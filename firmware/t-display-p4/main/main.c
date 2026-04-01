/*
 * BLUE-NET ETH Cable Tester — T-Display P4
 * =========================================
 * ESP32-P4  +  LAN8720A RMII Ethernet  +  RGB 800×480 LCD (ST7262)
 *
 * WiFi  = management interface — reports results to BLUE-NET dashboard
 * ETH   = cable-under-test port (link-state only, no IP stack needed)
 *
 * Build:  idf.py build
 * Flash:  idf.py -p /dev/ttyUSB0 flash monitor
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"

#include "ui.h"

// ─────────────────────────────────────────────────────────────────────────────
//  USER CONFIG — set your WiFi credentials and server address here
// ─────────────────────────────────────────────────────────────────────────────
#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID        "YOUR_WIFI_SSID"
#endif
#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"
#endif
#ifndef CONFIG_BLUENET_SERVER
// Server address of your BLUE-NET Node.js server (no trailing slash)
#define CONFIG_BLUENET_SERVER   "http://192.168.1.100:3000"
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  LCD GPIO — T-Display P4 (ST7262, 800×480 RGB)
//  ⚠ Verify ALL pin numbers against your board schematic before flashing.
//  LilyGo's T-Display-P4 schematic: github.com/Xinyuan-LilyGO/T-Display-P4
// ─────────────────────────────────────────────────────────────────────────────
#define LCD_H_RES       800
#define LCD_V_RES       480
#define LCD_PCLK_HZ     (12 * 1000 * 1000)

#define LCD_PIN_PCLK    40
#define LCD_PIN_VSYNC   41
#define LCD_PIN_HSYNC   39
#define LCD_PIN_DE      42
#define LCD_PIN_BL      45   // backlight — set GPIO_NUM_NC (-1) if unused

// 16-bit RGB565 data bus D0–D15
#define LCD_PIN_D0   15
#define LCD_PIN_D1   16
#define LCD_PIN_D2   17
#define LCD_PIN_D3   18
#define LCD_PIN_D4   19
#define LCD_PIN_D5   20
#define LCD_PIN_D6   21
#define LCD_PIN_D7   22
#define LCD_PIN_D8    7
#define LCD_PIN_D9    8
#define LCD_PIN_D10   9
#define LCD_PIN_D11  10
#define LCD_PIN_D12  11
#define LCD_PIN_D13  12
#define LCD_PIN_D14  13
#define LCD_PIN_D15  14

// ─────────────────────────────────────────────────────────────────────────────
//  ETHERNET PHY GPIO — LAN8720A RMII
// ─────────────────────────────────────────────────────────────────────────────
#ifndef CONFIG_ETH_MDC_GPIO
#define CONFIG_ETH_MDC_GPIO      23
#endif
#ifndef CONFIG_ETH_MDIO_GPIO
#define CONFIG_ETH_MDIO_GPIO     24
#endif
#ifndef CONFIG_ETH_PHY_RST_GPIO
#define CONFIG_ETH_PHY_RST_GPIO  -1   // -1 = no dedicated reset pin
#endif
#ifndef CONFIG_ETH_PHY_ADDR
#define CONFIG_ETH_PHY_ADDR       0
#endif

static const char *TAG = "ETH-TESTER";

// ─── Shared state (written by event handlers, read by heartbeat task) ─────────
static volatile bool   s_eth_link   = false;
static volatile int    s_link_speed = 0;      // 10 / 100 / 1000
static volatile bool   s_link_full  = false;
static volatile char   s_device_ip[40] = "0.0.0.0";
static volatile bool   s_wifi_up    = false;

// ─── LVGL mutex — any lv_* call outside the lvgl_task must take this ──────────
static SemaphoreHandle_t s_lvgl_mutex = NULL;

static bool lvgl_lock(uint32_t ms) {
    return xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static void lvgl_unlock(void) { xSemaphoreGiveRecursive(s_lvgl_mutex); }

// ─────────────────────────────────────────────────────────────────────────────
//  LCD + LVGL initialisation
// ─────────────────────────────────────────────────────────────────────────────
static esp_lcd_panel_handle_t s_panel = NULL;

// LVGL flush callback — called by LVGL to write a rectangle to the display
static void lcd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_panel,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void lcd_init(void) {
    // ── Backlight ──────────────────────────────────────────────────────────────
    if (LCD_PIN_BL >= 0) {
        gpio_config_t bl = {
            .pin_bit_mask = BIT64(LCD_PIN_BL),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&bl));
        gpio_set_level(LCD_PIN_BL, 1);
        ESP_LOGI(TAG, "Backlight ON (GPIO %d)", LCD_PIN_BL);
    }

    // ── RGB panel ─────────────────────────────────────────────────────────────
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src   = LCD_CLK_SRC_PLL160M,
        .timings   = {
            .pclk_hz            = LCD_PCLK_HZ,
            .h_res              = LCD_H_RES,
            .v_res              = LCD_V_RES,
            .hsync_back_porch   = 40,
            .hsync_front_porch  = 20,
            .hsync_pulse_width  = 1,
            .vsync_back_porch   = 8,
            .vsync_front_porch  = 4,
            .vsync_pulse_width  = 1,
            .flags.pclk_active_neg = 0,
        },
        .data_width      = 16,
        .num_fbs         = 2,          // double-buffer in PSRAM
        .pclk_gpio_num   = LCD_PIN_PCLK,
        .vsync_gpio_num  = LCD_PIN_VSYNC,
        .hsync_gpio_num  = LCD_PIN_HSYNC,
        .de_gpio_num     = LCD_PIN_DE,
        .disp_gpio_num   = GPIO_NUM_NC,
        .data_gpio_nums  = {
            LCD_PIN_D0,  LCD_PIN_D1,  LCD_PIN_D2,  LCD_PIN_D3,
            LCD_PIN_D4,  LCD_PIN_D5,  LCD_PIN_D6,  LCD_PIN_D7,
            LCD_PIN_D8,  LCD_PIN_D9,  LCD_PIN_D10, LCD_PIN_D11,
            LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
        },
        .flags.fb_in_psram = 1,
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "LCD panel init OK (%dx%d)", LCD_H_RES, LCD_V_RES);

    // ── LVGL 9 ────────────────────────────────────────────────────────────────
    lv_init();
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();

    // Two 40-line draw buffers in PSRAM
    size_t buf_bytes = LCD_H_RES * 40 * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(buf1 && buf2 && "PSRAM alloc failed — check CONFIG_ESP_PSRAM_SUPPORT");

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, lcd_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "LVGL 9 ready");
}

// ─────────────────────────────────────────────────────────────────────────────
//  WiFi (management interface — reports results to BLUE-NET server)
// ─────────────────────────────────────────────────────────────────────────────
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      10

static EventGroupHandle_t s_wifi_eg;
static int s_wifi_retry = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_up = false;
        if (s_wifi_retry < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry++;
            ESP_LOGW(TAG, "WiFi retry %d/%d", s_wifi_retry, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
        if (lvgl_lock(100)) { ui_set_status("WIFI LOST"); lvgl_unlock(); }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf((char *)s_device_ip, sizeof(s_device_ip),
                 IPSTR, IP2STR(&ev->ip_info.ip));
        s_wifi_up    = true;
        s_wifi_retry = 0;
        ESP_LOGI(TAG, "WiFi IP: %s", (char *)s_device_ip);
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
        if (lvgl_lock(100)) {
            ui_set_wifi_ip((char *)s_device_ip);
            lvgl_unlock();
        }
    }
}

static bool wifi_init_sta(void) {
    s_wifi_eg = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h_wifi, h_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &h_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &h_ip));

    wifi_config_t wcfg = {
        .sta = {
            .ssid               = CONFIG_WIFI_SSID,
            .password           = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi '%s'...", CONFIG_WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(20000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected — IP %s", (char *)s_device_ip);
        return true;
    }
    ESP_LOGE(TAG, "WiFi connection failed");
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Ethernet (cable-under-test — link state only, no TCP/IP stack on this port)
// ─────────────────────────────────────────────────────────────────────────────
static esp_eth_handle_t s_eth_handle = NULL;

static void eth_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base != ETH_EVENT) return;

    switch (id) {
        case ETHERNET_EVENT_CONNECTED: {
            eth_duplex_t duplex;
            eth_speed_t  speed;
            esp_eth_ioctl(s_eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex);
            esp_eth_ioctl(s_eth_handle, ETH_CMD_G_SPEED,       &speed);

            s_link_full  = (duplex == ETH_DUPLEX_FULL);
            s_link_speed = (speed  == ETH_SPEED_10M)  ? 10  :
                           (speed  == ETH_SPEED_100M) ? 100 : 1000;
            s_eth_link   = true;

            ESP_LOGI(TAG, "CABLE LINKED — %dMbps %s-duplex",
                     s_link_speed, s_link_full ? "full" : "half");

            if (lvgl_lock(100)) {
                ui_set_eth_result(true, s_link_speed, s_link_full);
                lvgl_unlock();
            }
            break;
        }
        case ETHERNET_EVENT_DISCONNECTED:
            s_eth_link   = false;
            s_link_speed = 0;
            s_link_full  = false;
            ESP_LOGW(TAG, "CABLE UNLINKED");
            if (lvgl_lock(100)) {
                ui_set_eth_result(false, 0, false);
                lvgl_unlock();
            }
            break;

        default: break;
    }
}

static void eth_tester_init(void) {
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_gpio.mdc_num  = CONFIG_ETH_MDC_GPIO;
    emac_cfg.smi_gpio.mdio_num = CONFIG_ETH_MDIO_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr       = CONFIG_ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = CONFIG_ETH_PHY_RST_GPIO;
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_cfg);

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &s_eth_handle));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
    ESP_LOGI(TAG, "ETH tester ready — plug cable into ETH port");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Heartbeat task — POSTs cable status to BLUE-NET server every 10 s
// ─────────────────────────────────────────────────────────────────────────────
static void heartbeat_task(void *arg) {
    uint32_t uptime_s = 0;
    vTaskDelay(pdMS_TO_TICKS(3000));   // let WiFi settle

    while (1) {
        uptime_s += 10;

        if (s_wifi_up) {
            char body[220];
            snprintf(body, sizeof(body),
                "{\"ip\":\"%s\","
                "\"eth_link\":%s,"
                "\"link_speed\":%d,"
                "\"link_duplex\":\"%s\","
                "\"uptime_s\":%lu}",
                (char *)s_device_ip,
                s_eth_link  ? "true" : "false",
                s_link_speed,
                s_link_full ? "full" : "half",
                (unsigned long)uptime_s);

            esp_http_client_config_t http_cfg = {
                .url        = CONFIG_BLUENET_SERVER "/api/tdisplay/heartbeat",
                .method     = HTTP_METHOD_POST,
                .timeout_ms = 5000,
            };
            esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, body, strlen(body));

            esp_err_t err = esp_http_client_perform(client);
            if (err != ESP_OK)
                ESP_LOGW(TAG, "Heartbeat failed: %s", esp_err_to_name(err));

            esp_http_client_cleanup(client);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  LVGL tasks
// ─────────────────────────────────────────────────────────────────────────────
static void lvgl_tick_task(void *arg) {
    // Feeds the LVGL millisecond tick — must run consistently
    while (1) {
        lv_tick_inc(5);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void lvgl_task(void *arg) {
    // Drives LVGL animations, timers, and flush
    while (1) {
        uint32_t delay_ms = 10;
        if (lvgl_lock(portMAX_DELAY)) {
            delay_ms = lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms < 1 ? 1 : delay_ms > 50 ? 50 : delay_ms));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  app_main
// ─────────────────────────────────────────────────────────────────────────────
void app_main(void) {
    // ── NVS (required by WiFi) ────────────────────────────────────────────────
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ── Display + LVGL ────────────────────────────────────────────────────────
    lcd_init();

    // Tick task: pinned to core 1 so it doesn't compete with network tasks
    xTaskCreatePinnedToCore(lvgl_tick_task, "lv_tick", 2048, NULL, 6, NULL, 1);

    // Build UI
    if (lvgl_lock(portMAX_DELAY)) {
        ui_init();
        lvgl_unlock();
    }

    // LVGL render task: core 1
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);

    // ── WiFi ──────────────────────────────────────────────────────────────────
    if (lvgl_lock(100)) { ui_set_status("CONNECTING WIFI..."); lvgl_unlock(); }

    bool wifi_ok = wifi_init_sta();

    if (!wifi_ok) {
        if (lvgl_lock(100)) { ui_set_status("WIFI FAILED — OFFLINE"); lvgl_unlock(); }
        ESP_LOGW(TAG, "Running without WiFi — display-only mode");
    }

    // ── Ethernet (cable tester) ───────────────────────────────────────────────
    eth_tester_init();

    // ── Heartbeat ─────────────────────────────────────────────────────────────
    // Runs on core 0 alongside WiFi/network stack
    xTaskCreatePinnedToCore(heartbeat_task, "heartbeat", 8192, NULL, 2, NULL, 0);

    ESP_LOGI(TAG, "BLUE-NET ETH Cable Tester running");
}
