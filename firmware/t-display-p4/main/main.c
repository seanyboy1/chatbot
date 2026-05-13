/*
 * BLUE-NET ETH Cable Tester — T-Display P4
 * ESP32-P4 + MIPI DSI HI8561 (540×1168) + LAN8720A RMII Ethernet
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth_netif_glue.h"
#include "lwip/ip4_addr.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_ldo_regulator.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_interface.h"
#include "lvgl.h"
#include "ui.h"
#include "rm69a10_driver.h"

// ── User config ───────────────────────────────────────────────────────────────
#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID        "seanyboy"
#endif
#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD    "Soscaredhere"
#endif
#ifndef CONFIG_BLUENET_SERVER
#define CONFIG_BLUENET_SERVER   "http://192.168.1.100:3000"
#endif

// ── Display — RM69A10 AMOLED 568×1232 ────────────────────────────────────────
#define LCD_H_RES   568
#define LCD_V_RES  1232

// ── XL9535 I2C GPIO expander ──────────────────────────────────────────────────
#define I2C_SDA_GPIO       GPIO_NUM_7
#define I2C_SCL_GPIO       GPIO_NUM_8
#define XL9535_ADDR        0x20
#define XL9535_REG_OUT0    0x02
#define XL9535_REG_OUT1    0x03
#define XL9535_REG_CFG0    0x06
#define XL9535_REG_CFG1    0x07
// Port 0 bits
#define XL_3V3_EN    (1 << 0)  // IO0
#define XL_DISP_RST  (1 << 2)  // IO2
#define XL_TOUCH_RST (1 << 3)  // IO3
#define XL_TOUCH_INT (1 << 4)  // IO4 (input)
#define XL_ETH_RST   (1 << 5)  // IO5 — LAN8720 nRST (active-low)
#define XL_5V_EN     (1 << 6)  // IO6
// Port 1 bits — XL9535 maps IO10..IO17 to port-1 bits 0..7 (offset -10)
#define XL_VCCA_EN  (1 << 0)   // IO10 → port1 bit0

// ── GT9895 touch controller ───────────────────────────────────────────────────
#define GT9895_ADDR         0x5D
#define GT9895_REG_TOUCH    {0x00, 0x01, 0x03, 0x08}
#define GT9895_MAX_X        1060
#define GT9895_MAX_Y        2400

// ── AMOLED has no backlight LED — brightness via command 0x51 ─────────────────

// ── ETH PHY ───────────────────────────────────────────────────────────────────
#ifndef CONFIG_ETH_MDC_GPIO
#define CONFIG_ETH_MDC_GPIO      31
#endif
#ifndef CONFIG_ETH_MDIO_GPIO
#define CONFIG_ETH_MDIO_GPIO     52
#endif
#ifndef CONFIG_ETH_PHY_RST_GPIO
#define CONFIG_ETH_PHY_RST_GPIO  51
#endif
#ifndef CONFIG_ETH_PHY_ADDR
#define CONFIG_ETH_PHY_ADDR       1
#endif

static const char *TAG = "ETH-TESTER";

// ── Shared state ──────────────────────────────────────────────────────────────
static volatile bool   s_eth_link   = false;
static volatile int    s_link_speed = 0;
static volatile bool   s_link_full  = false;
static volatile char   s_device_ip[40] = "0.0.0.0";
static volatile bool   s_wifi_up    = false;

// ── Flap / port locator ───────────────────────────────────────────────────────
static volatile bool   s_flap_active      = false;
static volatile int    s_flap_interval_ms = 1000;

// ── LVGL mutex ────────────────────────────────────────────────────────────────
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static bool lvgl_lock(uint32_t ms) {
    return xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void lvgl_unlock(void) { xSemaphoreGiveRecursive(s_lvgl_mutex); }

// ── XL9535 ────────────────────────────────────────────────────────────────────
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_xl9535  = NULL;
static i2c_master_dev_handle_t s_gt9895  = NULL;
static uint8_t s_p0 = 0, s_p1 = 0;

static void xl9535_wr(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_master_transmit(s_xl9535, buf, 2, pdMS_TO_TICKS(100));
}

static void xl9535_p0(uint8_t mask, bool hi) {
    if (hi) s_p0 |= mask; else s_p0 &= ~mask;
    xl9535_wr(XL9535_REG_OUT0, s_p0);
}

static void xl9535_p1(uint8_t mask, bool hi) {
    if (hi) s_p1 |= mask; else s_p1 &= ~mask;
    xl9535_wr(XL9535_REG_OUT1, s_p1);
}

static void xl9535_init(void) {
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bc, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return;
    }

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = XL9535_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &dc, &s_xl9535);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XL9535 add device failed: %s", esp_err_to_name(err));
        return;
    }

    // All outputs low first
    xl9535_wr(XL9535_REG_OUT0, 0x00);
    xl9535_wr(XL9535_REG_OUT1, 0x00);
    // IO0=3V3_EN, IO2=DISP_RST, IO3=TOUCH_RST, IO5=ETH_RST, IO6=5V_EN = outputs; IO4=TOUCH_INT = input
    xl9535_wr(XL9535_REG_CFG0, (uint8_t)~(XL_3V3_EN | XL_DISP_RST | XL_TOUCH_RST | XL_ETH_RST | XL_5V_EN));
    // IO10=VCCA_EN = output
    xl9535_wr(XL9535_REG_CFG1, 0xFE);
    ESP_LOGI(TAG, "XL9535 OK");

    // Release ETH PHY from XL9535 reset — GPIO51 drives the actual reset sequence
    xl9535_p0(XL_ETH_RST, true);

    // Power-on sequence matching LilyGo screen_lvgl/main.cpp exactly:
    // VCCA stays LOW (not enabled for RM69A10)
    xl9535_p1(XL_VCCA_EN, false);

    // 5V: HIGH → 200ms → LOW → 200ms → HIGH (stays HIGH = 5V on)
    xl9535_p0(XL_5V_EN, true);  vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_5V_EN, false); vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_5V_EN, true);

    // 3V3: LOW → 200ms → HIGH → 200ms → LOW (active-low: ends LOW = enabled)
    xl9535_p0(XL_3V3_EN, false); vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_3V3_EN, true);  vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_3V3_EN, false);

    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "Power sequence done — LDO and RST next");
}

// ── RM69A10 init handled by rm69a10_driver component (LilyGo's driver) ────────

// ── LCD / LVGL init ───────────────────────────────────────────────────────────
static esp_lcd_panel_handle_t s_dpi_panel = NULL;

static void lcd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_dpi_panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    lv_display_flush_ready(disp);
}

static void lcd_init(void) {
    xl9535_init();

    // LDO for MIPI DSI PHY — 1830mV (matches LilyGo exactly)
    esp_ldo_channel_handle_t ldo_mipi = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = 3,
        .voltage_mv = 1830,
    };
    esp_err_t ldo_err = esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi);
    ESP_LOGI(TAG, "LDO ch3 1830mV: %s", esp_err_to_name(ldo_err));

    vTaskDelay(pdMS_TO_TICKS(100));

    // Display reset AFTER LDO (matches LilyGo order)
    xl9535_p0(XL_DISP_RST, true);  vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_DISP_RST, false); vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_DISP_RST, true);  vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "RST done");

    // DSI bus
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t dsi_cfg = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 1000,
    };
    esp_err_t dsi_err = esp_lcd_new_dsi_bus(&dsi_cfg, &dsi_bus);
    ESP_LOGI(TAG, "DSI bus: %s", esp_err_to_name(dsi_err));
    if (dsi_err != ESP_OK) {
        ESP_LOGE(TAG, "DSI bus failed — skipping display init");
        return;
    }

    // DBI IO — send RM69A10 init commands via LP mode BEFORE starting DPI pixel stream
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    esp_err_t dbi_err = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io);
    ESP_LOGI(TAG, "DBI IO: %s", esp_err_to_name(dbi_err));
    if (dbi_err != ESP_OK) {
        ESP_LOGE(TAG, "DBI IO failed — skipping display init");
        return;
    }

    // DPI config — matches LilyGo t_display_p4_driver.cpp exactly
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel    = 0,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 60,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = 1,  // panel manages its own scanout FB
        .video_timing = {
            .h_size            = LCD_H_RES,
            .v_size            = LCD_V_RES,
            .hsync_pulse_width = 50,
            .hsync_back_porch  = 150,
            .hsync_front_porch = 50,
            .vsync_pulse_width = 40,
            .vsync_back_porch  = 120,
            .vsync_front_porch = 80,
        },
        .flags.use_dma2d    = false,
    };

    // Use LilyGo's RM69A10 panel driver — wraps DPI panel and sends init commands
    rm69a10_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus    = dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num   = 2,
        },
    };
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num  = -1,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel  = 16,
        .vendor_config   = &vendor_cfg,
    };
    esp_err_t rm_err = esp_lcd_new_panel_rm69a10(io, &dev_cfg, &s_dpi_panel);
    ESP_LOGI(TAG, "RM69A10 panel create: %s", esp_err_to_name(rm_err));
    if (rm_err != ESP_OK) {
        ESP_LOGE(TAG, "RM69A10 panel create failed — skipping");
        return;
    }

    esp_err_t init_err = esp_lcd_panel_init(s_dpi_panel);
    ESP_LOGI(TAG, "Panel init: %s", esp_err_to_name(init_err));
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed — skipping");
        return;
    }

    ESP_LOGI(TAG, "LCD init complete");
}

// WiFi disabled — ESP32-P4 WiFi requires C6 coprocessor with esp_hosted firmware.
// When the C6 has that firmware flashed, re-add espressif/esp_wifi_remote to
// idf_component.yml and restore wifi_init_sta().
static bool wifi_init_sta(void) { return false; }

// ── Ethernet ──────────────────────────────────────────────────────────────────
static esp_eth_handle_t s_eth_handle = NULL;

static void eth_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base != ETH_EVENT) return;
    switch (id) {
        case ETHERNET_EVENT_CONNECTED: {
            eth_duplex_t dup; eth_speed_t spd;
            esp_eth_ioctl(s_eth_handle, ETH_CMD_G_DUPLEX_MODE, &dup);
            esp_eth_ioctl(s_eth_handle, ETH_CMD_G_SPEED, &spd);
            s_link_full  = (dup == ETH_DUPLEX_FULL);
            s_link_speed = (spd == ETH_SPEED_10M) ? 10 :
                           (spd == ETH_SPEED_100M) ? 100 : 1000;
            s_eth_link = true;
            if (lvgl_lock(100)) {
                ui_set_eth_link(true);
                ui_set_eth_speed(s_link_speed, s_link_full);
                char log_buf[48];
                snprintf(log_buf, sizeof(log_buf), "[ETH] Link UP %dMbps %s",
                         s_link_speed, s_link_full ? "FULL" : "HALF");
                ui_eth_log_append(log_buf);
                lvgl_unlock();
            }
            break;
        }
        case ETHERNET_EVENT_DISCONNECTED:
            s_eth_link = false; s_link_speed = 0; s_link_full = false;
            if (lvgl_lock(100)) {
                ui_set_eth_link(false);
                ui_eth_log_append("[ETH] Link DOWN");
                lvgl_unlock();
            }
            break;
        default: break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data) {
    if (id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;

        char ip_str[20], sub_str[20], gw_str[20], mac_str[24];
        snprintf(ip_str,  sizeof(ip_str),  IPSTR, IP2STR(&ev->ip_info.ip));
        snprintf(sub_str, sizeof(sub_str), IPSTR, IP2STR(&ev->ip_info.netmask));
        snprintf(gw_str,  sizeof(gw_str),  IPSTR, IP2STR(&ev->ip_info.gw));

        uint8_t mac[6] = {0};
        if (s_eth_handle)
            esp_eth_ioctl(s_eth_handle, ETH_CMD_G_MAC_ADDR, mac);
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "[DHCP] IP: %s", ip_str);

        if (lvgl_lock(100)) {
            ui_set_eth_dhcp(ip_str);
            ui_set_eth_net_info(mac_str, sub_str, gw_str, "---", "none");
            ui_eth_log_append(log_buf);
            lvgl_unlock();
        }
    } else if (id == IP_EVENT_ETH_LOST_IP) {
        if (lvgl_lock(100)) {
            ui_set_eth_dhcp("waiting...");
            ui_eth_log_append("[DHCP] IP lost");
            lvgl_unlock();
        }
    }
}

static void eth_tester_init(void) {
    eth_mac_config_t mc = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t ec = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    ec.smi_gpio.mdc_num  = CONFIG_ETH_MDC_GPIO;
    ec.smi_gpio.mdio_num = CONFIG_ETH_MDIO_GPIO;
    ESP_LOGI(TAG, "ETH init: MDC=%d MDIO=%d RST=%d", CONFIG_ETH_MDC_GPIO, CONFIG_ETH_MDIO_GPIO, CONFIG_ETH_PHY_RST_GPIO);
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&ec, &mc);
    eth_phy_config_t pc = ETH_PHY_DEFAULT_CONFIG();
    pc.phy_addr        = ESP_ETH_PHY_ADDR_AUTO;  // scan all addresses
    pc.reset_gpio_num  = CONFIG_ETH_PHY_RST_GPIO;
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&pc);
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&eth_cfg, &s_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ETH driver install failed (%s) — ETH disabled", esp_err_to_name(err));
        if (lvgl_lock(100)) { ui_set_eth_link(false); lvgl_unlock(); }
        return;
    }
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, ip_event_handler, NULL);

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(s_eth_handle));

    esp_eth_start(s_eth_handle);
}

// ── Stats / Heartbeat tasks ───────────────────────────────────────────────────
#define STATS_BUF_LEN 512
static char s_sbuf[STATS_BUF_LEN];
static int  s_sbuf_pos = 0;

static int json_int(const char *b, const char *k, int fb) {
    char s[48]; snprintf(s, sizeof(s), "\"%s\":", k);
    const char *p = strstr(b, s); if (!p) return fb;
    p += strlen(s); while (*p==' ') p++;
    return (int)strtol(p, NULL, 10);
}
static bool json_bool(const char *b, const char *k, bool fb) {
    char s[48]; snprintf(s, sizeof(s), "\"%s\":", k);
    const char *p = strstr(b, s); if (!p) return fb;
    p += strlen(s); while (*p==' ') p++;
    if (strncmp(p,"true",4)==0) return true;
    if (strncmp(p,"false",5)==0) return false;
    return fb;
}

static esp_err_t stats_http_evt(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        s_sbuf_pos + evt->data_len < STATS_BUF_LEN - 1) {
        memcpy(s_sbuf + s_sbuf_pos, evt->data, evt->data_len);
        s_sbuf_pos += evt->data_len;
        s_sbuf[s_sbuf_pos] = '\0';
    }
    return ESP_OK;
}

static void stats_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(8000));
    while (1) {
        if (s_wifi_up) {
            s_sbuf_pos = 0; memset(s_sbuf, 0, sizeof(s_sbuf));
            esp_http_client_config_t hc = {
                .url = CONFIG_BLUENET_SERVER "/api/tdisplay/stats",
                .method = HTTP_METHOD_GET, .timeout_ms = 8000,
                .event_handler = stats_http_evt,
            };
            esp_http_client_handle_t cl = esp_http_client_init(&hc);
            esp_err_t err = esp_http_client_perform(cl);
            if (err == ESP_OK && s_sbuf_pos > 0) {
                if (lvgl_lock(200)) {
                    ui_dash_set_stats(
                        (int)(esp_get_free_heap_size() / 1024),
                        (int)(esp_get_free_internal_heap_size() / 1024),
                        0, 0);
                    ui_net_set_status(json_bool(s_sbuf,"webhook_ok",false) ? "OK" : "FAIL");
                    lvgl_unlock();
                }
            } else {
                if (lvgl_lock(200)) { ui_net_set_status("NO SERVER"); lvgl_unlock(); }
            }
            esp_http_client_cleanup(cl);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void heartbeat_task(void *arg) {
    uint32_t uptime_s = 0;
    vTaskDelay(pdMS_TO_TICKS(3000));
    while (1) {
        uptime_s += 10;
        if (s_wifi_up) {
            char body[220];
            snprintf(body, sizeof(body),
                "{\"ip\":\"%s\",\"eth_link\":%s,\"link_speed\":%d,"
                "\"link_duplex\":\"%s\",\"uptime_s\":%lu}",
                (char*)s_device_ip, s_eth_link?"true":"false",
                s_link_speed, s_link_full?"full":"half",
                (unsigned long)uptime_s);
            esp_http_client_config_t hc = {
                .url = CONFIG_BLUENET_SERVER "/api/tdisplay/heartbeat",
                .method = HTTP_METHOD_POST, .timeout_ms = 5000,
            };
            esp_http_client_handle_t cl = esp_http_client_init(&hc);
            esp_http_client_set_header(cl, "Content-Type", "application/json");
            esp_http_client_set_post_field(cl, body, strlen(body));
            esp_http_client_perform(cl);
            esp_http_client_cleanup(cl);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ── GT9895 touch ──────────────────────────────────────────────────────────────
static void touch_init(void) {
    if (!s_i2c_bus) { ESP_LOGE(TAG, "I2C bus not ready — touch skipped"); return; }

    // Touch reset via XL9535 IO3 (runs here, after ETH init, to avoid PHY interference)
    xl9535_p0(XL_TOUCH_RST, true);  vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_TOUCH_RST, false); vTaskDelay(pdMS_TO_TICKS(200));
    xl9535_p0(XL_TOUCH_RST, true);  vTaskDelay(pdMS_TO_TICKS(200));

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = GT9895_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dc, &s_gt9895);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT9895 add device failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "GT9895 touch init OK");
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (!s_gt9895) return;
    static const uint8_t addr[4] = GT9895_REG_TOUCH;
    uint8_t buf[16] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_gt9895,
                        addr, sizeof(addr), buf, sizeof(buf),
                        pdMS_TO_TICKS(20));
    if (err != ESP_OK || buf[2] == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    int32_t raw_x = (int32_t)(buf[10] | (buf[11] << 8));
    int32_t raw_y = (int32_t)(buf[12] | (buf[13] << 8));
    data->point.x = (lv_coord_t)(raw_x * LCD_H_RES / GT9895_MAX_X);
    data->point.y = (lv_coord_t)(raw_y * LCD_V_RES / GT9895_MAX_Y);
    data->state   = LV_INDEV_STATE_PRESSED;
}

// ── LVGL tasks ────────────────────────────────────────────────────────────────
static void lvgl_tick_task(void *arg) {
    while (1) { lv_tick_inc(5); vTaskDelay(pdMS_TO_TICKS(5)); }
}
static void lvgl_task(void *arg) {
    while (1) {
        uint32_t d = 10;
        if (lvgl_lock(portMAX_DELAY)) { d = lv_timer_handler(); lvgl_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(d < 1 ? 1 : d > 50 ? 50 : d));
    }
}

// ── Flap task — toggles ETH link on/off to locate cable/port ─────────────────
static void flap_task(void *arg) {
    while (s_flap_active) {
        int ivl    = s_flap_interval_ms;
        int off_ms = ivl / 2;
        int on_ms  = ivl - off_ms;
        esp_eth_stop(s_eth_handle);
        vTaskDelay(pdMS_TO_TICKS(off_ms));
        if (!s_flap_active) break;
        esp_eth_start(s_eth_handle);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
    }
    s_flap_active = false;
    vTaskDelete(NULL);
}

static void flap_cb(bool start, int interval_ms) {
    if (start) {
        if (s_flap_active || !s_eth_handle) return;
        s_flap_interval_ms = (interval_ms > 0) ? interval_ms : 1000;
        s_flap_active = true;
        xTaskCreate(flap_task, "flap", 4096, NULL, 3, NULL);
    } else {
        s_flap_active = false;
    }
}

static void network_init_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    wifi_init_sta();
    eth_tester_init();
    // Touch reset runs here — after ETH init — to avoid any XL9535 IO3 interference
    touch_init();
    if (s_gt9895) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
    }
    // Transition splash → home regardless of network status
    if (lvgl_lock(500)) { ui_splash_done(); lvgl_unlock(); }
    vTaskDelete(NULL);
}

// Runs LVGL + ui_init from a task with a large PSRAM stack
static void lvgl_init_task(void *arg) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)arg;

    size_t buf_size = LCD_H_RES * (LCD_V_RES / 10) * sizeof(uint16_t);
    void *lvgl_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lvgl_buf) lvgl_buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(lvgl_buf);

    lv_init();
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, lcd_flush_cb);
    lv_display_set_buffers(disp, lvgl_buf, NULL, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ui_set_flap_cb(flap_cb);
    ui_init();

    xTaskCreate(lvgl_tick_task, "lv_tick", 2048,  NULL, 5, NULL);
    xTaskCreate(lvgl_task,      "lv_task", 16384, NULL, 4, NULL);
    xTaskCreate(stats_task,     "stats",   6144,  NULL, 2, NULL);
    xTaskCreate(heartbeat_task, "hb",      4096,  NULL, 1, NULL);
    xTaskCreate(network_init_task, "net",  8192,  NULL, 3, NULL);

    vTaskDelete(NULL);
}

// ── app_main ──────────────────────────────────────────────────────────────────
void app_main(void) {
    // Display hardware first
    lcd_init();
    if (!s_dpi_panel) {
        ESP_LOGE(TAG, "Display init failed — halting");
        while (1) vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // Fade brightness in
    for (int i = 0; i < 255; i += 5) {
        set_rm69a10_brightness(s_dpi_panel, (uint8_t)i);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_rm69a10_brightness(s_dpi_panel, 255);

    // NVS + system init
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    // Spawn LVGL init in a task with a large PSRAM stack so ui_init()
    // doesn't overflow the 8KB main task stack
    StackType_t  *ui_stack = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    StaticTask_t *ui_tcb   = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    assert(ui_stack && ui_tcb);
    xTaskCreateStaticPinnedToCore(lvgl_init_task, "lvgl_init",
                                  32768 / sizeof(StackType_t),
                                  s_dpi_panel, 4,
                                  ui_stack, ui_tcb, 0);
}
