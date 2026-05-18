/*
 * BLUE-NET ETH Cable Tester — T-Display P4
 * ESP32-P4 + MIPI DSI HI8561 (540×1168) + LAN8720A RMII Ethernet
 */

#include <string.h>
#include <time.h>
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
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include <stdio.h>
// esp_sntp.h removed — time is synced via wifi_at_get_epoch() using C6 AT firmware SNTP
#include "esp_system.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_interface.h"
#include "lvgl.h"
#include "ui.h"
#include "rm69a10_driver.h"
#include "ping/ping_sock.h"
#include "driver/i2s_std.h"
#include "esp_crt_bundle.h"
#include "esp_check.h"
#include "wifi_at.h"
#include <math.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "minimp3.h"

// ── User config ───────────────────────────────────────────────────────────────
#define CONFIG_WIFI_SSID        "your_wifi_ssid"
#define CONFIG_WIFI_PASSWORD    "your_wifi_password"
#define CONFIG_WIFI_SSID2       "your_fallback_ssid"
#define CONFIG_WIFI_PASSWORD2   "your_fallback_password"
#ifndef CONFIG_BLUENET_SERVER
#define CONFIG_BLUENET_SERVER   "http://10.0.0.242:3000"
#endif
// POSIX TZ string — US Pacific (PT): UTC-8 standard, UTC-7 daylight
#define CONFIG_TIMEZONE         "PST8PDT,M3.2.0,M11.1.0"

// ── Weather / audio config ────────────────────────────────────────────────────
// Open-Meteo: free, no auth, compact JSON. Change lat/lon for your area.
#define CONFIG_WEATHER_LAT      "32.72"      // San Diego, CA default
#define CONFIG_WEATHER_LON      "-117.15"
#define CONFIG_WEATHER_TZ       "America/Los_Angeles"
// NOAA alert area code (2-letter US state, or "CAZ" zone prefix, etc.)
#define CONFIG_NOAA_AREA        "CA"
// Richland WA WWF56 (162.450 MHz) — 64 kbps, live now, Pacific NW NOAA
// Switch to http://radio.weatherusa.net/NWR/KIG98.mp3 when KIG98 feeder is back
#define CONFIG_NOAA_STREAM_URL  "http://wxradio.org:8000/WA-Richland-WWF56"

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
#define XL_C6_WAKE  (1 << 3)   // IO13 → port1 bit3  ESP32-C6 WAKE
#define XL_C6_EN    (1 << 4)   // IO14 → port1 bit4  ESP32-C6 EN (power)
#define XL_SD_EN    (1 << 5)   // IO15 → port1 bit5 (SD card power)

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
static volatile char   s_eth_ip[40]    = "0.0.0.0";
static volatile bool   s_eth_ip_ready  = false;
static volatile bool   s_wifi_up    = false;
static volatile bool   s_wifi_status_dirty = false;

// ── Cable / speed test ────────────────────────────────────────────────────────
static volatile bool   s_speed_test_running = false;

// ── Flap / port locator ───────────────────────────────────────────────────────
static volatile bool   s_flap_active      = false;
static volatile int    s_flap_interval_ms = 1000;
static volatile bool   s_locate_active    = false;

// ── Gateway IP (set when DHCP fires) ─────────────────────────────────────────
static ip4_addr_t s_gw_ip;

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

    // Port0: all outputs low (ETH_RST, DISP_RST etc driven by init sequence below)
    xl9535_wr(XL9535_REG_OUT0, 0x00);
    // Port1: C6_EN and C6_WAKE HIGH before setting direction — prevents glitching C6 off
    xl9535_wr(XL9535_REG_OUT1, XL_C6_EN | XL_C6_WAKE);
    // IO0=3V3_EN, IO2=DISP_RST, IO3=TOUCH_RST, IO5=ETH_RST, IO6=5V_EN = outputs; IO4=TOUCH_INT = input
    xl9535_wr(XL9535_REG_CFG0, (uint8_t)~(XL_3V3_EN | XL_DISP_RST | XL_TOUCH_RST | XL_ETH_RST | XL_5V_EN));
    // IO10=VCCA_EN, IO13=C6_WAKE, IO14=C6_EN, IO15=SD_EN = outputs; C6 already powered via OUT1 above
    xl9535_wr(XL9535_REG_CFG1, (uint8_t)~(XL_VCCA_EN | XL_C6_WAKE | XL_C6_EN | XL_SD_EN));
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

/* Shadow framebuffer — kept in PSRAM, updated by lcd_flush_cb on every partial
   render.  screenshot_task reads from it without holding the LVGL lock. */
static uint16_t *s_shadow_fb = NULL;

static void lcd_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_dpi_panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    if (s_shadow_fb) {
        int w = area->x2 - area->x1 + 1;
        const uint16_t *src = (const uint16_t *)px_map;
        for (int y = area->y1; y <= area->y2; y++) {
            memcpy(&s_shadow_fb[y * LCD_H_RES + area->x1],
                   src + (y - area->y1) * w,
                   w * sizeof(uint16_t));
        }
    }
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

// ── WiFi reconnect button callback ───────────────────────────────────────────
static void wifi_reconnect_cb(void) {
    if (lvgl_lock(200)) { ui_net_set_status("RECONNECTING..."); lvgl_unlock(); }
    wifi_at_reconnect();
}

// ── WiFi scan ─────────────────────────────────────────────────────────────────
static void scan_result_cb(const char *ssid, int rssi) {
    if (lvgl_lock(100)) {
        ui_net_add_scan_result(ssid, rssi);
        lvgl_unlock();
    }
}

static void wifi_scan_task(void *arg) {
    if (lvgl_lock(200)) { ui_net_set_status("SCANNING..."); ui_net_clear_scan(); lvgl_unlock(); }
    wifi_at_scan(scan_result_cb);
    if (lvgl_lock(200)) { ui_net_set_status("SCAN DONE"); lvgl_unlock(); }
    vTaskDelete(NULL);
}

static void wifi_scan_cb(void) {
    xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 3, NULL);
}

// ── WiFi connect to new network ───────────────────────────────────────────────
typedef struct { char ssid[64]; char pass[64]; } wifi_con_args_t;

static void wifi_connect_task(void *arg) {
    wifi_con_args_t *a = (wifi_con_args_t *)arg;
    if (lvgl_lock(200)) { ui_net_set_status("CONNECTING..."); lvgl_unlock(); }
    bool ok = wifi_at_connect(a->ssid, a->pass);
    free(a);
    if (ok) {
        s_wifi_up = true;
        char ip[40] = "";
        wifi_at_get_ip(ip, sizeof(ip));
        strlcpy((char *)s_device_ip, ip, sizeof(s_device_ip));
        if (lvgl_lock(200)) {
            ui_net_set_status("WIFI OK");
            ui_set_wifi_ip(ip);
            lvgl_unlock();
        }
    } else {
        if (lvgl_lock(200)) { ui_net_set_status("CONNECT FAIL"); lvgl_unlock(); }
    }
    vTaskDelete(NULL);
}

static void wifi_connect_cb(const char *ssid, const char *pass) {
    wifi_con_args_t *a = malloc(sizeof(wifi_con_args_t));
    if (!a) return;
    strlcpy(a->ssid, ssid, sizeof(a->ssid));
    strlcpy(a->pass, pass ? pass : "", sizeof(a->pass));
    xTaskCreate(wifi_connect_task, "wifi_con", 4096, a, 3, NULL);
}

// ── ES8311 audio codec + I2S ──────────────────────────────────────────────────
// ES8311: I2C on bus 1 (SDA=GPIO20, SCL=GPIO21), I2S TX on I2S_NUM_0
// Pins: MCLK=13, BCLK=12, WS=9, DOUT(DAC)=10, DIN(ADC)=11
#define ES8311_I2C_SDA    GPIO_NUM_20
#define ES8311_I2C_SCL    GPIO_NUM_21
#define ES8311_I2C_ADDR   0x18
#define ES8311_SAMPLE_RATE 44100
#define I2S_MCLK_MULTI    256

static i2s_chan_handle_t        s_i2s_tx        = NULL;
static volatile bool            s_audio_playing = false;
static volatile bool            s_audio_stop    = false;

/* Write one ES8311 register via the new i2c_master API */
static esp_err_t es8311_reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t audio_codec_init(void)
{
    /* ── I2C bus for ES8311 (I2C_NUM_1, SDA=20, SCL=21) ── */
    i2c_master_bus_config_t bc = {
        .i2c_port              = I2C_NUM_1,
        .sda_io_num            = ES8311_I2C_SDA,
        .scl_io_num            = ES8311_I2C_SCL,
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt     = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t audio_bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bc, &audio_bus), TAG, "audio i2c bus");

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ES8311_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    i2c_master_dev_handle_t codec = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(audio_bus, &dc, &codec), TAG, "es8311 add");

    /* ── ES8311 register init: 44100 Hz, 16-bit, I2S, MCLK=256×Fs ── */
    es8311_reg_write(codec, 0x00, 0x1F); /* reset */
    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_reg_write(codec, 0x00, 0x00);
    es8311_reg_write(codec, 0x00, 0x80); /* power-on */
    es8311_reg_write(codec, 0x01, 0x3F); /* enable all clocks, use MCLK pin */
    es8311_reg_write(codec, 0x06, 0x00); /* SCLK not inverted */
    es8311_reg_write(codec, 0x02, 0x00); /* pre_div=1, pre_multi=1x */
    es8311_reg_write(codec, 0x03, 0x10); /* adc_osr=0x10, single-speed */
    es8311_reg_write(codec, 0x04, 0x10); /* dac_osr=0x10 */
    es8311_reg_write(codec, 0x05, 0x00); /* adc_div=1, dac_div=1 */
    es8311_reg_write(codec, 0x06, 0x03); /* bclk_div=4 (i.e. 4-1=3) */
    es8311_reg_write(codec, 0x07, 0x00); /* lrck_h=0 */
    es8311_reg_write(codec, 0x08, 0xFF); /* lrck_l=255 → LRCK=MCLK/256=44100 */
    es8311_reg_write(codec, 0x00, 0x80); /* slave serial port */
    es8311_reg_write(codec, 0x09, 0x0C); /* DAC SDP: I2S 16-bit */
    es8311_reg_write(codec, 0x0A, 0x0C); /* ADC SDP: I2S 16-bit */
    es8311_reg_write(codec, 0x0D, 0x01); /* power up analog */
    es8311_reg_write(codec, 0x0E, 0x02); /* enable PGA + ADC modulator */
    es8311_reg_write(codec, 0x12, 0x00); /* power up DAC */
    es8311_reg_write(codec, 0x13, 0x10); /* enable HP drive */
    es8311_reg_write(codec, 0x1C, 0x6A); /* ADC equalizer bypass */
    es8311_reg_write(codec, 0x37, 0x08); /* bypass DAC equalizer */
    es8311_reg_write(codec, 0x17, 0xC8); /* ADC gain */
    es8311_reg_write(codec, 0x14, 0x1A); /* analog mic, max PGA gain */
    es8311_reg_write(codec, 0x32, 0xCB); /* DAC volume 80% */
    ESP_LOGI(TAG, "ES8311 init OK");

    /* ── I2S TX channel ── */
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_i2s_tx, NULL), TAG, "i2s channel");

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(ES8311_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_13,
            .bclk = GPIO_NUM_12,
            .ws   = GPIO_NUM_9,
            .dout = GPIO_NUM_10,
            .din  = GPIO_NUM_11,
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTI;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std), TAG, "i2s std mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "i2s enable");

    ESP_LOGI(TAG, "Audio init OK");
    return ESP_OK;
}

// ── NOAA weather data fetch ───────────────────────────────────────────────────
#define WEATHER_BUF_SIZE 8192
static char   *s_weather_buf     = NULL;
static int     s_weather_buf_pos = 0;

static esp_err_t weather_http_evt(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && s_weather_buf &&
        s_weather_buf_pos + evt->data_len < WEATHER_BUF_SIZE - 1) {
        memcpy(s_weather_buf + s_weather_buf_pos, evt->data, evt->data_len);
        s_weather_buf_pos += evt->data_len;
        s_weather_buf[s_weather_buf_pos] = '\0';
    }
    return ESP_OK;
}

/* Minimal JSON string extract: find key, return its string value */
static bool json_str(const char *json, const char *key, char *out, size_t out_len) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (!end) return false;
        size_t n = (size_t)(end - p);
        if (n >= out_len) n = out_len - 1;
        memcpy(out, p, n); out[n] = '\0';
        return true;
    } else {
        /* numeric */
        const char *end = p;
        while (*end && *end != ',' && *end != '}' && *end != '\n') end++;
        size_t n = (size_t)(end - p);
        if (n >= out_len) n = out_len - 1;
        memcpy(out, p, n); out[n] = '\0';
        return true;
    }
}

/* Fetch url into a malloc'd buffer.  Uses WiFi AT if ETH not ready. Returns buf or NULL. */
static char *do_http_get(const char *url) {
    if (s_wifi_up && !s_eth_ip_ready) {
        char *buf = NULL;
        int len = wifi_at_http_get(url, &buf);
        return (len > 0) ? buf : NULL;
    }
    /* ETH path — esp_http_client */
    s_weather_buf_pos = 0;
    memset(s_weather_buf, 0, WEATHER_BUF_SIZE);
    esp_http_client_config_t hc = {
        .url               = url,
        .timeout_ms        = 10000,
        .event_handler     = weather_http_evt,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    esp_err_t err = esp_http_client_perform(cl);
    esp_http_client_cleanup(cl);
    if (err != ESP_OK || s_weather_buf_pos == 0) return NULL;
    char *copy = malloc(s_weather_buf_pos + 1);
    if (copy) { memcpy(copy, s_weather_buf, s_weather_buf_pos); copy[s_weather_buf_pos] = '\0'; }
    return copy;
}

static void weather_fetch_task(void *arg) {
    if (lvgl_lock(200)) { ui_weather_radio_set_status("FETCHING...", true); lvgl_unlock(); }

    s_weather_buf = malloc(WEATHER_BUF_SIZE);
    if (!s_weather_buf) { goto done_no_buf; }

    /* ── Current weather via Open-Meteo ── */
    {
        char url[256];
        snprintf(url, sizeof(url),
            "https://api.open-meteo.com/v1/forecast"
            "?latitude=" CONFIG_WEATHER_LAT
            "&longitude=" CONFIG_WEATHER_LON
            "&current_weather=true"
            "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,weathercode"
            "&temperature_unit=fahrenheit"
            "&timezone=" CONFIG_WEATHER_TZ
            "&forecast_days=1");
        char *body = do_http_get(url);
        if (body) {
            char temp[16]="--", wcode[8]="0", tmax[16]="--", tmin[16]="--";

            /* Current temp lives inside "current_weather":{...} */
            const char *cw = strstr(body, "\"current_weather\"");
            if (cw) {
                json_str(cw, "temperature", temp, sizeof(temp));
                json_str(cw, "weathercode",  wcode, sizeof(wcode));
            }

            /* High/low: "daily":{"time":[...],"temperature_2m_max":[N],...}
               Skip past "daily_units" to avoid hitting the string "°F" first. */
            const char *daily = strstr(body, "\"daily\":{");
            if (!daily) daily = strstr(body, "\"daily\": {");
            if (daily) {
                /* temperature_2m_max":[N] */
                const char *mx = strstr(daily, "\"temperature_2m_max\":[");
                if (mx) {
                    mx += strlen("\"temperature_2m_max\":[");
                    const char *e = mx; while (*e && *e != ',' && *e != ']') e++;
                    size_t n = e - mx; if (n > 0 && n < 15) { memcpy(tmax, mx, n); tmax[n]='\0'; }
                }
                const char *mn = strstr(daily, "\"temperature_2m_min\":[");
                if (mn) {
                    mn += strlen("\"temperature_2m_min\":[");
                    const char *e = mn; while (*e && *e != ',' && *e != ']') e++;
                    size_t n = e - mn; if (n > 0 && n < 15) { memcpy(tmin, mn, n); tmin[n]='\0'; }
                }
            }

            int wc = atoi(wcode);
            const char *desc =
                wc == 0 ? "Clear sky" : wc <= 3 ? "Partly cloudy" :
                wc <= 9 ? "Foggy" : wc <= 19 ? "Drizzle" :
                wc <= 29 ? "Rain" : wc <= 39 ? "Snow" :
                wc <= 49 ? "Fog" : wc <= 59 ? "Drizzle" :
                wc <= 69 ? "Rain" : wc <= 79 ? "Snow" :
                wc <= 84 ? "Rain showers" : wc <= 94 ? "Thunderstorm" : "Hail";

            char line[128];
            snprintf(line, sizeof(line), "NOW: %s°F  %s", temp, desc);
            if (lvgl_lock(200)) { ui_weather_radio_append(line); lvgl_unlock(); }
            snprintf(line, sizeof(line), "HIGH: %s°F  LOW: %s°F", tmax, tmin);
            if (lvgl_lock(200)) { ui_weather_radio_append(line); lvgl_unlock(); }
            free(body);
        } else {
            if (lvgl_lock(200)) { ui_weather_radio_append("Weather fetch failed"); lvgl_unlock(); }
        }
    }

    /* ── Active NOAA alerts ── */
    {
        char url[192];
        snprintf(url, sizeof(url),
            "https://api.weather.gov/alerts/active"
            "?area=" CONFIG_NOAA_AREA
            "&status=actual&message_type=alert&limit=3");
        char *body = do_http_get(url);
        if (body) {
            int count = 0;
            const char *p = body;
            while ((p = strstr(p, "\"headline\"")) != NULL && count < 3) {
                char headline[128] = {0};
                if (json_str(p, "headline", headline, sizeof(headline))) {
                    if (lvgl_lock(200)) {
                        char line[140];
                        snprintf(line, sizeof(line), "ALERT: %s", headline);
                        ui_weather_radio_append(line);
                        lvgl_unlock();
                    }
                    count++;
                }
                p += 10;
            }
            if (count == 0) {
                if (lvgl_lock(200)) { ui_weather_radio_append("No active alerts"); lvgl_unlock(); }
            }
            free(body);
        }
    }

    free(s_weather_buf);
    s_weather_buf = NULL;
done_no_buf:
    if (lvgl_lock(200)) { ui_weather_radio_set_status("UPDATED", false); lvgl_unlock(); }
    vTaskDelete(NULL);
}

static void weather_fetch_cb(void) {
    xTaskCreate(weather_fetch_task, "wx_fetch", 8192, NULL, 3, NULL);
}

// ── NOAA radio audio streaming ────────────────────────────────────────────────
#define AUDIO_STREAM_BUF  4096
#define AUDIO_PCM_BUF    (MINIMP3_MAX_SAMPLES_PER_FRAME * 2)  /* stereo int16 */

static void noaa_radio_task(void *arg) {
    s_audio_playing = true;
    s_audio_stop    = false;

    if (!s_i2s_tx) {
        ESP_LOGE(TAG, "Audio not initialized");
        if (lvgl_lock(200)) { ui_weather_radio_set_status("AUDIO NOT READY", false); lvgl_unlock(); }
        s_audio_playing = false;
        vTaskDelete(NULL);
        return;
    }

    if (!s_eth_ip_ready && !s_wifi_up) {
        ESP_LOGE(TAG, "No network — cannot stream");
        if (lvgl_lock(200)) { ui_weather_radio_set_status("NO NETWORK", false); lvgl_unlock(); }
        s_audio_playing = false;
        vTaskDelete(NULL);
        return;
    }

    uint8_t *stream_buf = malloc(AUDIO_STREAM_BUF);
    int16_t *pcm_buf    = malloc(AUDIO_PCM_BUF * sizeof(int16_t));
    mp3dec_t *dec       = malloc(sizeof(mp3dec_t));
    if (!stream_buf || !pcm_buf || !dec) {
        ESP_LOGE(TAG, "Audio OOM");
        free(stream_buf); free(pcm_buf); free(dec);
        s_audio_playing = false;
        vTaskDelete(NULL);
        return;
    }
    mp3dec_init(dec);

    if (lvgl_lock(200)) { ui_weather_radio_set_status("CONNECTING...", true); lvgl_unlock(); }

    /* ── WiFi AT streaming path ── */
    if (s_wifi_up && !s_eth_ip_ready) {
        // Status callback: called from wifi_at during CIPSTART so UI shows progress
        void stream_status_update(const char *msg) {
            if (lvgl_lock(100)) { ui_weather_radio_set_status(msg, true); lvgl_unlock(); }
        }
        wifi_at_stream_t stream = wifi_at_stream_open(CONFIG_NOAA_STREAM_URL, stream_status_update);
        if (!stream) {
            const char *err = wifi_at_stream_last_error();
            char emsg[64];
            if (err && err[0]) snprintf(emsg, sizeof(emsg), "AT:%s", err);
            else snprintf(emsg, sizeof(emsg), "STREAM FAILED");
            ESP_LOGE(TAG, "AT stream open failed: %s", emsg);
            if (lvgl_lock(200)) { ui_weather_radio_set_status(emsg, false); lvgl_unlock(); }
            free(stream_buf); free(pcm_buf); free(dec);
            s_audio_playing = false;
            vTaskDelete(NULL);
            return;
        }
        if (lvgl_lock(200)) { ui_weather_radio_set_status("LIVE  NOAA RADIO", true); lvgl_unlock(); }

        uint8_t *ring = malloc(AUDIO_STREAM_BUF * 4);
        int ring_fill = 0;
        int total_rx = 0, total_frames = 0;
        int diag_tick = 0;

        /* Pre-buffer 8 KB before starting playback to avoid initial stutter */
        while (!s_audio_stop && ring_fill < AUDIO_STREAM_BUF * 2) {
            int got = wifi_at_stream_read(stream, ring + ring_fill,
                                         AUDIO_STREAM_BUF * 4 - ring_fill, 200);
            if (got > 0) { ring_fill += got; total_rx += got; }
            else if (got < 0) goto wifi_stream_done;
        }

        while (!s_audio_stop) {
            /* Burst-fill: keep reading until ring has 12 KB or no more data.
               Don't break on 0-return — keep trying so underruns are minimised.
               Each read returns up to 480 bytes; 16 iterations × 40 ms = 640 ms max. */
            for (int r = 0; r < 16 && ring_fill < AUDIO_STREAM_BUF * 3 && !s_audio_stop; r++) {
                int got = wifi_at_stream_read(stream, ring + ring_fill,
                                             AUDIO_STREAM_BUF * 4 - ring_fill, 40);
                if (got > 0) { ring_fill += got; total_rx += got; r = 0; }
                else if (got < 0) goto wifi_stream_done;
                /* got==0: keep looping rather than breaking */
            }

            // Show diagnostics every 20 loops
            if (++diag_tick % 20 == 0) {
                char dstat[48];
                if (total_frames > 0)
                    snprintf(dstat, sizeof(dstat), "FRMS:%d RX:%d", total_frames, total_rx);
                else
                    snprintf(dstat, sizeof(dstat), "RX:%d NO FRMS", total_rx);
                if (lvgl_lock(100)) { ui_weather_radio_set_status(dstat, true); lvgl_unlock(); }
            }

            if (ring_fill < 512) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(dec, ring, ring_fill, pcm_buf, &info);
            if (info.frame_bytes > 0) {
                memmove(ring, ring + info.frame_bytes, ring_fill - info.frame_bytes);
                ring_fill -= info.frame_bytes;
            } else {
                if (ring_fill > 0) { memmove(ring, ring + 1, --ring_fill); }
                continue;
            }
            if (samples > 0) {
                total_frames++;
                /* minimp3 returns samples-per-channel; PCM buffer holds
                   samples*channels interleaved int16 values. Mono needs
                   in-place expansion to stereo before the I2S write. */
                if (info.channels == 1) {
                    for (int i = samples - 1; i >= 0; i--) {
                        pcm_buf[i*2+1] = pcm_buf[i];
                        pcm_buf[i*2]   = pcm_buf[i];
                    }
                }
                size_t written;
                i2s_channel_write(s_i2s_tx, pcm_buf,
                                  (size_t)samples * 2 * sizeof(int16_t),
                                  &written, pdMS_TO_TICKS(500));
            }
        }
        wifi_stream_done:
        free(ring);
        wifi_at_stream_close(stream);
        free(stream_buf); free(pcm_buf); free(dec);
        if (lvgl_lock(200)) { ui_weather_radio_set_status("RADIO STOPPED", false); lvgl_unlock(); }
        s_audio_playing = false;
        vTaskDelete(NULL);
        return;
    }

    /* ── Ethernet streaming path (esp_http_client) ── */
    esp_http_client_config_t hc = {
        .url        = CONFIG_NOAA_STREAM_URL,
        .timeout_ms = 10000,
        .buffer_size = AUDIO_STREAM_BUF,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (esp_http_client_open(cl, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Stream open failed");
        if (lvgl_lock(200)) { ui_weather_radio_set_status("STREAM FAILED", false); lvgl_unlock(); }
        goto audio_done;
    }
    esp_http_client_fetch_headers(cl);

    if (lvgl_lock(200)) { ui_weather_radio_set_status("LIVE  NOAA RADIO", true); lvgl_unlock(); }

    /* Ring buffer for incoming data */
    uint8_t *ring = malloc(AUDIO_STREAM_BUF * 4);
    int ring_fill = 0;

    while (!s_audio_stop) {
        /* Refill ring buffer */
        if (ring_fill < AUDIO_STREAM_BUF * 2) {
            int got = esp_http_client_read(cl, (char *)(ring + ring_fill),
                                           AUDIO_STREAM_BUF * 4 - ring_fill);
            if (got > 0) ring_fill += got;
            else if (got < 0) break;
        }

        if (ring_fill < 128) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        /* Decode one MP3 frame */
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(dec, ring, ring_fill, pcm_buf, &info);
        if (info.frame_bytes > 0) {
            memmove(ring, ring + info.frame_bytes, ring_fill - info.frame_bytes);
            ring_fill -= info.frame_bytes;
        } else {
            /* Skip a byte to resync */
            if (ring_fill > 0) { memmove(ring, ring + 1, --ring_fill); }
            continue;
        }

        if (samples > 0) {
            if (info.channels == 1) {
                for (int i = samples - 1; i >= 0; i--) {
                    pcm_buf[i*2+1] = pcm_buf[i];
                    pcm_buf[i*2]   = pcm_buf[i];
                }
            }
            size_t written;
            i2s_channel_write(s_i2s_tx, pcm_buf,
                              (size_t)samples * 2 * sizeof(int16_t),
                              &written, pdMS_TO_TICKS(500));
        }
    }
    free(ring);

audio_done:
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    free(stream_buf); free(pcm_buf); free(dec);

    if (lvgl_lock(200)) { ui_weather_radio_set_status("RADIO STOPPED", false); lvgl_unlock(); }
    s_audio_playing = false;
    vTaskDelete(NULL);
}

static void weather_radio_cb(bool start) {
    if (start) {
        if (!s_audio_playing) {
            /* 32KB stack: minimp3 decode tables + HTTP client need headroom */
            xTaskCreate(noaa_radio_task, "noaa_radio", 32768, NULL, 5, NULL);
        }
    } else {
        s_audio_stop = true;
    }
}

/* ── Terminal "beep" command — generates 1 kHz sine for 0.5 s ── */
static void beep_task(void *arg) {
    if (!s_i2s_tx) {
        if (lvgl_lock(100)) { ui_term_append("Audio not ready"); lvgl_unlock(); }
        vTaskDelete(NULL);
        return;
    }
    const int sr = 44100;
    const int dur_frames = sr / 2;          /* 0.5 s */
    const int chunk = 441;                  /* 10 ms per write */
    int16_t *buf = malloc(chunk * 2 * sizeof(int16_t));
    if (!buf) { vTaskDelete(NULL); return; }

    int written_frames = 0;
    while (written_frames < dur_frames) {
        int n = (dur_frames - written_frames < chunk) ? (dur_frames - written_frames) : chunk;
        for (int i = 0; i < n; i++) {
            float t = (float)(written_frames + i) / sr;
            int16_t s = (int16_t)(28000.0f * sinf(2.0f * (float)M_PI * 1000.0f * t));
            buf[i*2]   = s;
            buf[i*2+1] = s;
        }
        size_t wr;
        i2s_channel_write(s_i2s_tx, buf, n * 2 * sizeof(int16_t), &wr, pdMS_TO_TICKS(200));
        written_frames += n;
    }
    free(buf);
    if (lvgl_lock(100)) { ui_term_append("Beep done"); lvgl_unlock(); }
    vTaskDelete(NULL);
}

static void term_cmd_cb(const char *cmd, char *out, size_t out_len) {
    if (strcmp(cmd, "beep") == 0) {
        xTaskCreate(beep_task, "beep", 4096, NULL, 5, NULL);
    } else if (strcmp(cmd, "ip") == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "WiFi IP: %s", (char *)s_device_ip);
        if (lvgl_lock(100)) { ui_term_append(buf); lvgl_unlock(); }
    } else {
        snprintf(out, out_len, "Unknown: %s", cmd);
        if (lvgl_lock(100)) { ui_term_append(out); lvgl_unlock(); }
    }
}

// ── Ethernet ──────────────────────────────────────────────────────────────────
static esp_eth_handle_t s_eth_handle = NULL;

static esp_err_t phy_read(uint32_t reg, uint32_t *val) {
    esp_eth_phy_reg_rw_data_t rw = { .reg_addr = reg, .reg_value_p = val };
    return esp_eth_ioctl(s_eth_handle, ETH_CMD_READ_PHY_REG, &rw);
}

static esp_err_t phy_write(uint32_t reg, uint32_t val) {
    esp_eth_phy_reg_rw_data_t rw = { .reg_addr = reg, .reg_value_p = &val };
    return esp_eth_ioctl(s_eth_handle, ETH_CMD_WRITE_PHY_REG, &rw);
}

static void phy_page(uint32_t page) {
    phy_write(0x14, page & 0x1F);
}

static volatile bool s_cable_test_running = false;

// ── Ping helper ───────────────────────────────────────────────────────────────
static SemaphoreHandle_t s_ping_done = NULL;
static uint32_t s_ping_avg_ms = 0;
static uint32_t s_ping_loss   = 0;

static void ping_on_ping_success(esp_ping_handle_t hdl, void *args) {
    uint32_t elapsed;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    s_ping_avg_ms += elapsed;
}

static void ping_on_ping_end(esp_ping_handle_t hdl, void *args) {
    uint32_t sent, received;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &sent, sizeof(sent));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY,   &received, sizeof(received));
    if (received > 0) s_ping_avg_ms /= received;
    s_ping_loss = sent - received;
    if (s_ping_done) xSemaphoreGive(s_ping_done);
}

static int run_ping_gw(void) {
    if (s_gw_ip.addr == 0) return -1;
    if (!s_ping_done) s_ping_done = xSemaphoreCreateBinary();
    s_ping_avg_ms = 0;
    s_ping_loss   = 0;

    ip_addr_t target;
    target.type        = IPADDR_TYPE_V4;
    target.u_addr.ip4  = s_gw_ip;

    esp_ping_config_t cfg    = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr          = target;
    cfg.count                = 3;
    cfg.interval_ms          = 200;
    cfg.timeout_ms           = 1000;
    cfg.task_stack_size      = 4096;
    cfg.task_prio            = 2;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_on_ping_success,
        .on_ping_end     = ping_on_ping_end,
    };

    esp_ping_handle_t hdl;
    if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) return -1;
    esp_ping_start(hdl);
    xSemaphoreTake(s_ping_done, pdMS_TO_TICKS(5000));
    esp_ping_delete_session(hdl);
    return (s_ping_loss < 3) ? (int)s_ping_avg_ms : -1;
}

static void cable_test_task(void *arg) {
    s_cable_test_running = true;

    if (lvgl_lock(200)) {
        ui_cable_set_pair(0, "...", -1, false);
        ui_cable_set_pair(1, "...", -1, false);
        ui_cable_set_pair(2, "...", -1, false);
        ui_cable_set_pair(3, "...", -1, false);
        ui_cable_set_summary(false, "...");
        lvgl_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    bool linked = s_eth_link;
    int  speed  = s_link_speed;
    bool full   = s_link_full;

    /* MDI-X state from CSSR page 16, reg 0x1E bit 3 */
    bool mdix = false;
    phy_page(16);
    uint32_t cssr = 0;
    phy_read(0x1E, &cssr);
    phy_page(0);
    mdix = (cssr >> 3) & 1;

    const char *pair_st = linked ? "OK" : "NO LINK";
    bool pair_ok = linked;

    if (lvgl_lock(300)) {
        for (int p = 0; p < 4; p++)
            ui_cable_set_pair(p, pair_st, -1, pair_ok);

        char summary[24];
        if (linked)
            snprintf(summary, sizeof(summary), "%dM %s", speed, full ? "FULL" : "HALF");
        else
            snprintf(summary, sizeof(summary), "NO CABLE");
        ui_cable_set_summary(linked, mdix ? "X-OVER" : "STRAIGHT");
        ui_eth_log_append(linked ? summary : "[TEST] no cable");
        lvgl_unlock();
    }

    ESP_LOGI(TAG, "Cable test: link=%s %dM %s MDI%s",
             linked?"UP":"DOWN", speed, full?"FD":"HD", mdix?"X":"");

    if (linked) {
        int rtt = run_ping_gw();
        if (lvgl_lock(200)) {
            char ping_buf[32];
            if (rtt >= 0)
                snprintf(ping_buf, sizeof(ping_buf), "PING GW: %dms", rtt);
            else
                snprintf(ping_buf, sizeof(ping_buf), "PING GW: TIMEOUT");
            ui_eth_log_append(ping_buf);
            lvgl_unlock();
        }
    }

    /* Append result to SD log */
    {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        char line[128];
        snprintf(line, sizeof(line),
            "%04d-%02d-%02d %02d:%02d:%02d | %s | %s | %s | %dM %s\n",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec,
            linked ? "PASS" : "FAIL",
            linked ? "CABLE OK" : "NO CABLE",
            mdix ? "X-OVER" : "STRAIGHT",
            speed, full ? "FD" : "HD");

        sd_pwr_ctrl_handle_t log_pwr = NULL;
        sd_pwr_ctrl_ldo_config_t log_ldo = { .ldo_chan_id = 4 };
        if (sd_pwr_ctrl_new_on_chip_ldo(&log_ldo, &log_pwr) == ESP_OK) {
            sdmmc_host_t log_host = SDMMC_HOST_DEFAULT();
            log_host.slot = SDMMC_HOST_SLOT_0;
            log_host.pwr_ctrl_handle = log_pwr;
            sdmmc_slot_config_t log_slot = SDMMC_SLOT_CONFIG_DEFAULT();
            log_slot.width = 4;
            log_slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
            esp_vfs_fat_sdmmc_mount_config_t log_mnt = {
                .format_if_mount_failed = false,
                .max_files = 4,
            };
            sdmmc_card_t *log_card = NULL;
            if (esp_vfs_fat_sdmmc_mount("/sdcard", &log_host, &log_slot,
                                        &log_mnt, &log_card) == ESP_OK) {
                FILE *f = fopen("/sdcard/cable_log.txt", "a");
                if (f) { fputs(line, f); fclose(f); }
                esp_vfs_fat_sdcard_unmount("/sdcard", log_card);
            }
            sd_pwr_ctrl_del_on_chip_ldo(log_pwr);
        }
    }

    s_cable_test_running = false;
    vTaskDelete(NULL);
}

static void cable_test_start_cb(void) {
    if (!s_cable_test_running)
        xTaskCreate(cable_test_task, "cdt", 12288, NULL, 3, NULL);
}

// ── Speed test ───────────────────────────────────────────────────────────────
#define STRINGIFY2(x) #x
#define STRINGIFY(x)  STRINGIFY2(x)
#define SPEEDTEST_DL_SIZE  524288   /* 512 KB */
#define SPEEDTEST_UL_SIZE  262144   /* 256 KB */

static esp_err_t speedtest_http_evt(esp_http_client_event_t *evt) {
    size_t *count = (size_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && count)
        *count += evt->data_len;
    return ESP_OK;
}

static void speed_test_task(void *arg) {
    s_speed_test_running = true;

    if (lvgl_lock(200)) {
        ui_speed_set_result("-- / --");
        ui_eth_log_append("[SPEED] starting...");
        lvgl_unlock();
    }

    /* Wait for IP */
    for (int i = 0; i < 50 && !s_eth_ip_ready; i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    if (!s_eth_ip_ready) {
        if (lvgl_lock(200)) { ui_speed_set_result("NO ETH"); ui_eth_log_append("[SPEED] no IP"); lvgl_unlock(); }
        s_speed_test_running = false;
        vTaskDelete(NULL);
        return;
    }

    float dl_mbps = -1.0f, ul_mbps = -1.0f;

    /* ── Download test ── */
    {
        size_t rx = 0;
        esp_http_client_config_t hc = {
            .url            = CONFIG_BLUENET_SERVER "/api/speedtest/download?size=" STRINGIFY(SPEEDTEST_DL_SIZE),
            .timeout_ms     = 15000,
            .event_handler  = speedtest_http_evt,
            .user_data      = &rx,
        };
        esp_http_client_handle_t cl = esp_http_client_init(&hc);
        int64_t t0 = esp_timer_get_time();
        esp_err_t err = esp_http_client_perform(cl);
        int64_t dt_us = esp_timer_get_time() - t0;
        esp_http_client_cleanup(cl);
        if (err == ESP_OK && rx > 0 && dt_us > 0)
            dl_mbps = (float)rx * 8.0f / ((float)dt_us / 1e6f) / 1e6f;
        ESP_LOGI(TAG, "Speed DL: %.1f Mbps (%zu B in %lld ms)", dl_mbps, rx, dt_us / 1000);
    }

    /* ── Upload test ── */
    {
        uint8_t *ul_buf = heap_caps_malloc(SPEEDTEST_UL_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ul_buf) {
            memset(ul_buf, 0x55, SPEEDTEST_UL_SIZE);
            esp_http_client_config_t hc = {
                .url        = CONFIG_BLUENET_SERVER "/api/speedtest/upload",
                .method     = HTTP_METHOD_POST,
                .timeout_ms = 15000,
            };
            esp_http_client_handle_t cl = esp_http_client_init(&hc);
            esp_http_client_set_header(cl, "Content-Type", "application/octet-stream");
            esp_http_client_set_post_field(cl, (const char *)ul_buf, SPEEDTEST_UL_SIZE);
            int64_t t0 = esp_timer_get_time();
            esp_err_t err = esp_http_client_perform(cl);
            int64_t dt_us = esp_timer_get_time() - t0;
            esp_http_client_cleanup(cl);
            free(ul_buf);
            if (err == ESP_OK && dt_us > 0)
                ul_mbps = (float)SPEEDTEST_UL_SIZE * 8.0f / ((float)dt_us / 1e6f) / 1e6f;
            ESP_LOGI(TAG, "Speed UL: %.1f Mbps in %lld ms", ul_mbps, dt_us / 1000);
        }
    }

    /* Update UI and log */
    if (lvgl_lock(300)) {
        char res[32];
        if (dl_mbps > 0 && ul_mbps > 0)
            snprintf(res, sizeof(res), "%.0f / %.0f Mbps", dl_mbps, ul_mbps);
        else if (dl_mbps > 0)
            snprintf(res, sizeof(res), "DL %.0f Mbps", dl_mbps);
        else
            snprintf(res, sizeof(res), "FAILED");
        ui_speed_set_result(res);

        char log_buf[48];
        snprintf(log_buf, sizeof(log_buf), "[SPEED] DL %.0f UL %.0f Mbps",
                 dl_mbps > 0 ? dl_mbps : 0.0f,
                 ul_mbps > 0 ? ul_mbps : 0.0f);
        ui_eth_log_append(log_buf);
        lvgl_unlock();
    }

    s_speed_test_running = false;
    vTaskDelete(NULL);
}

static void speed_test_start_cb(void) {
    if (!s_speed_test_running && s_eth_ip_ready)
        xTaskCreate(speed_test_task, "spd", 20480, NULL, 3, NULL);
}

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
        s_gw_ip.addr = ev->ip_info.gw.addr;

        uint8_t mac[6] = {0};
        if (s_eth_handle)
            esp_eth_ioctl(s_eth_handle, ETH_CMD_G_MAC_ADDR, mac);
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        strlcpy((char *)s_eth_ip, ip_str, sizeof(s_eth_ip));
        s_eth_ip_ready = true;

        char log_buf[64];
        snprintf(log_buf, sizeof(log_buf), "[DHCP] IP: %s", ip_str);

        if (lvgl_lock(100)) {
            ui_set_eth_dhcp(ip_str);
            ui_set_eth_net_info(mac_str, sub_str, gw_str, "---", "none");
            ui_eth_log_append(log_buf);
            lvgl_unlock();
        }
    } else if (id == IP_EVENT_ETH_LOST_IP) {
        s_eth_ip_ready = false;
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

// ── Screenshot → SD card ─────────────────────────────────────────────────────
// SDMMC SLOT_0 on ESP32-P4. LilyGo pattern: sd_pwr_ctrl_new_on_chip_ldo with
// ldo_chan_id=4 assigned to host.pwr_ctrl_handle — the SDMMC driver controls
// the LDO timing internally. XL_SD_EN (IO15) still needed for slot power.
// FatFS struct must be in internal SRAM (CONFIG_FATFS_ALLOC_PREFER_EXTRAM=n +
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024) — PSRAM DMA causes IDMAC hangs.

static SemaphoreHandle_t s_screenshot_sem = NULL;

static void screenshot_cb(void) {
    if (s_screenshot_sem) xSemaphoreGive(s_screenshot_sem);
}

static void write_le16(uint8_t *b, uint16_t v) { b[0]=v; b[1]=v>>8; }
static void write_le32(uint8_t *b, uint32_t v) { b[0]=v; b[1]=v>>8; b[2]=v>>16; b[3]=v>>24; }

/* s_sd_stage encodes the failure point — shown on screen as "FAIL S<n>":
     raw esp_err_t from esp_vfs_fat_sdmmc_mount (e.g. 263 = timeout)
     6   = row_buf OOM
     500 = fopen failed */
static int s_sd_stage = 0;

static bool save_bmp(void) {
    s_sd_stage = 0;
    if (!s_shadow_fb) { s_sd_stage = -1; return false; }

    /* XL_SD_EN (IO15) is ACTIVE-LOW: xl9535_init drives it OUTPUT LOW = SD enabled.
       Never set it HIGH — that disables the card. LDO4 via sd_pwr_ctrl handles IO voltage. */

    sdmmc_card_t *card = NULL;
    bool mounted = false;
    bool success = false;
    sd_pwr_ctrl_handle_t pwr_ctrl = NULL;

    /* LDO4 power control: SDMMC driver sets 3.3V (rail) before CMD0. */
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = 4 };
    esp_err_t ldo_err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_ctrl);
    if (ldo_err != ESP_OK) {
        s_sd_stage = (int)ldo_err; goto done;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.pwr_ctrl_handle = pwr_ctrl;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = 4;
    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 0,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_cfg, &mount_cfg, &card);
    if (err != ESP_OK) {
        s_sd_stage = (int)err;
        goto done;
    }
    mounted = true;
    ESP_LOGI(TAG, "SD: mounted OK — %s %.0f MB",
             card->cid.name, (double)(card->csd.capacity / 2048));

    {
        static uint32_t s_shot_n = 0;
        char path[32];
        snprintf(path, sizeof(path), "/sdcard/S%07lu.BMP",
                 (unsigned long)(s_shot_n++ % 10000000));

        const int W = LCD_H_RES, H = LCD_V_RES;
        const int row_bytes = ((W * 3 + 3) & ~3);
        uint32_t pixel_data_size = (uint32_t)row_bytes * H;
        uint32_t file_size = 54 + pixel_data_size;

        uint8_t bmp_hdr[54] = {0};
        bmp_hdr[0]='B'; bmp_hdr[1]='M';
        write_le32(bmp_hdr+2,  file_size);
        write_le32(bmp_hdr+10, 54);
        write_le32(bmp_hdr+14, 40);
        write_le32(bmp_hdr+18, (uint32_t)W);
        write_le32(bmp_hdr+22, (uint32_t)(-(int32_t)H));
        write_le16(bmp_hdr+26, 1);
        write_le16(bmp_hdr+28, 24);
        write_le32(bmp_hdr+34, pixel_data_size);
        write_le32(bmp_hdr+38, 2835);
        write_le32(bmp_hdr+42, 2835);

        FILE *f = fopen(path, "wb");
        if (!f) {
            s_sd_stage = 500;
            ESP_LOGE(TAG, "fopen failed: %s", path);
            goto done;
        }
        fwrite(bmp_hdr, 1, 54, f);

        uint8_t *row_buf = heap_caps_malloc(row_bytes,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!row_buf) {
            s_sd_stage = 6;
            fclose(f);
            goto done;
        }

        for (int y = 0; y < H; y++) {
            const uint16_t *row = &s_shadow_fb[y * W];
            for (int x = 0; x < W; x++) {
                uint16_t c = row[x];
                row_buf[x*3+0] = (c & 0x001F) << 3;
                row_buf[x*3+1] = ((c >> 5) & 0x003F) << 2;
                row_buf[x*3+2] = (c >> 11) << 3;
            }
            for (int p = W*3; p < row_bytes; p++) row_buf[p] = 0;
            fwrite(row_buf, 1, row_bytes, f);
        }
        free(row_buf);
        fclose(f);
        ESP_LOGI(TAG, "Screenshot saved: %s", path);
        success = true;
    }

done:
    if (mounted) esp_vfs_fat_sdcard_unmount("/sdcard", card);
    if (pwr_ctrl) sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl);
    return success;
}

static void screenshot_task(void *arg) {
    s_screenshot_sem = xSemaphoreCreateBinary();
    while (1) {
        xSemaphoreTake(s_screenshot_sem, portMAX_DELAY);

        if (lvgl_lock(200)) {
            ui_eth_set_shot_status("SAVING...", true);
            lvgl_unlock();
        }

        /* Shadow framebuffer is always current — no LVGL lock needed here */
        bool ok = save_bmp();

        if (lvgl_lock(200)) {
            char fail_msg[24];
            if (!ok) snprintf(fail_msg, sizeof(fail_msg), "FAIL S%d", s_sd_stage);
            ui_eth_set_shot_status(ok ? LV_SYMBOL_OK " SAVED" : fail_msg, ok);
            lvgl_unlock();
        }
    }
}

// ── Battery ADC ───────────────────────────────────────────────────────────────
// T-Display P4: battery voltage divider on ADC1_CH0 (GPIO1).
// If reading is stuck at 0% or 100% unplugged, the pin needs adjusting.
#define BATT_ADC_CHAN   ADC_CHANNEL_0   // GPIO1

static void battery_task(void *arg) {
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id       = ADC_UNIT_1,
        .ulp_mode      = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed — battery disabled");
        vTaskDelete(NULL);
        return;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(adc, BATT_ADC_CHAN, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed — battery disabled");
        adc_oneshot_del_unit(adc);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        /* Average 4 samples to reduce noise */
        int sum = 0;
        for (int i = 0; i < 4; i++) {
            int raw = 0;
            if (adc_oneshot_read(adc, BATT_ADC_CHAN, &raw) == ESP_OK)
                sum += raw;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        int raw_avg = sum / 4;

        /* ADC_ATTEN_DB_11: ~0–3.9V full scale over 4095 counts
           Voltage divider 1:1 → batt_mv = raw_avg * 3900 / 4095 * 2 */
        int batt_mv = (int)((float)raw_avg * 7800.0f / 4095.0f);

        /* LiPo: 3000mV = 0%, 4200mV = 100% */
        int pct = (batt_mv - 3000) * 100 / (4200 - 3000);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;

        ESP_LOGI(TAG, "Batt: raw=%d batt_mv=%d pct=%d%%", raw_avg, batt_mv, pct);

        if (lvgl_lock(200)) {
            ui_set_battery(pct, false);
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void stats_task(void *arg) {
    while (!s_eth_ip_ready) vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(2000));
    uint32_t fail_delay = 30000;
    while (1) {
        if (s_eth_ip_ready) {
            s_sbuf_pos = 0; memset(s_sbuf, 0, sizeof(s_sbuf));
            esp_http_client_config_t hc = {
                .url = CONFIG_BLUENET_SERVER "/api/tdisplay/stats",
                .method = HTTP_METHOD_GET, .timeout_ms = 8000,
                .event_handler = stats_http_evt,
            };
            esp_http_client_handle_t cl = esp_http_client_init(&hc);
            esp_err_t err = esp_http_client_perform(cl);
            if (err == ESP_OK && s_sbuf_pos > 0) {
                fail_delay = 30000;
                if (lvgl_lock(200)) {
                    ui_dash_set_stats(
                        (int)(esp_get_free_heap_size() / 1024),
                        (int)(esp_get_free_internal_heap_size() / 1024),
                        0, 0);
                    ui_net_set_status(json_bool(s_sbuf,"webhook_ok",false) ? "OK" : "FAIL");
                    lvgl_unlock();
                }
            } else {
                /* Back off up to 5 min so failed attempts don't spam the log */
                fail_delay = (fail_delay < 300000) ? fail_delay * 2 : 300000;
                if (lvgl_lock(200)) { ui_net_set_status("NO SERVER"); lvgl_unlock(); }
            }
            esp_http_client_cleanup(cl);
        }
        vTaskDelay(pdMS_TO_TICKS(fail_delay));
    }
}

static void heartbeat_task(void *arg) {
    uint32_t uptime_s = 0;
    while (!s_eth_ip_ready) vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t fail_streak = 0;
    while (1) {
        uptime_s += 10;
        if (s_eth_ip_ready) {
            char body[256];
            snprintf(body, sizeof(body),
                "{\"ip\":\"%s\",\"wifi_ip\":\"%s\",\"eth_link\":%s,\"link_speed\":%d,"
                "\"link_duplex\":\"%s\",\"uptime_s\":%lu}",
                (char*)s_eth_ip, (char*)s_device_ip, s_eth_link?"true":"false",
                s_link_speed, s_link_full?"full":"half",
                (unsigned long)uptime_s);
            esp_http_client_config_t hc = {
                .url = CONFIG_BLUENET_SERVER "/api/tdisplay/heartbeat",
                .method = HTTP_METHOD_POST, .timeout_ms = 5000,
            };
            esp_http_client_handle_t cl = esp_http_client_init(&hc);
            esp_http_client_set_header(cl, "Content-Type", "application/json");
            esp_http_client_set_post_field(cl, body, strlen(body));
            esp_err_t err = esp_http_client_perform(cl);
            esp_http_client_cleanup(cl);
            fail_streak = (err == ESP_OK) ? 0 : fail_streak + 1;
        }
        /* After 3 consecutive failures back off to 2 min; reset when server returns */
        uint32_t delay = (fail_streak >= 3) ? 120000 : 10000;
        vTaskDelay(pdMS_TO_TICKS(delay));
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
        xTaskCreate(flap_task, "flap", 8192, NULL, 3, NULL);
    } else {
        s_flap_active = false;
    }
}

/* Speed/duplex changes require the driver to be stopped first. */
static void eth_set_speed(eth_speed_t speed, eth_duplex_t duplex, bool an) {
    esp_eth_stop(s_eth_handle);
    vTaskDelay(pdMS_TO_TICKS(100));
    bool an_val = an;
    esp_eth_ioctl(s_eth_handle, ETH_CMD_S_AUTONEGO, &an_val);
    if (!an) {
        esp_eth_ioctl(s_eth_handle, ETH_CMD_S_SPEED,       &speed);
        esp_eth_ioctl(s_eth_handle, ETH_CMD_S_DUPLEX_MODE, &duplex);
    }
    esp_eth_start(s_eth_handle);
}

/* Locate port: alternate 10M (amber LED) ↔ 100M (green LED) on the switch. */
static void locator_task(void *arg) {
    if (!s_eth_handle) { vTaskDelete(NULL); return; }

    while (s_locate_active) {
        /* 10M HD → switch LED turns AMBER */
        eth_set_speed(ETH_SPEED_10M, ETH_DUPLEX_HALF, false);
        for (int i = 0; i < 25 && s_locate_active; i++)
            vTaskDelay(pdMS_TO_TICKS(100));  /* 2.5 s */

        if (!s_locate_active) break;

        /* Auto-neg → re-negotiates 100M → switch LED turns GREEN */
        eth_set_speed(ETH_SPEED_100M, ETH_DUPLEX_FULL, true);
        for (int i = 0; i < 20 && s_locate_active; i++)
            vTaskDelay(pdMS_TO_TICKS(100));  /* 2 s */
    }

    /* Always restore auto-neg on exit */
    eth_set_speed(ETH_SPEED_100M, ETH_DUPLEX_FULL, true);
    vTaskDelete(NULL);
}

static void locate_start_cb(bool start) {
    s_locate_active = start;
    if (start)
        xTaskCreate(locator_task, "locate", 3072, NULL, 3, NULL);
}

// ── 1-second clock tick — updates time/date on all headers ───────────────────
static void time_tick_cb(void *arg) {
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    char time_buf[8], date_buf[12];
    strftime(time_buf, sizeof(time_buf), "%H:%M", &t);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &t);

    if (lvgl_lock(10)) {
        ui_set_time(time_buf);
        ui_set_time_date(date_buf);

        if (s_wifi_status_dirty) {
            s_wifi_status_dirty = false;
            if (s_wifi_up) {
                char wifi_info[64];
                snprintf(wifi_info, sizeof(wifi_info),
                         LV_SYMBOL_WIFI "  %s  |  %s",
                         CONFIG_WIFI_SSID, (char *)s_device_ip);
                ui_set_status(wifi_info);
                ui_set_wifi_ip((const char *)s_device_ip);
            } else {
                ui_set_status("CONNECTING...");
            }
        }

        lvgl_unlock();
    }
}

/* Callback used by wifi_at_init to power-cycle C6 via XL9535 */
static void c6_en_ctrl(bool high) {
    xl9535_p1(XL_C6_EN, high);
}

static void c6_init_task(void *arg) {
    /* C6 is already powered (C6_EN HIGH from xl9535_init).
       wifi_at_init will reset and re-init C6 via SDIO-AT using its
       factory AT firmware (v4.0.0.0) — no firmware flash required. */
    vTaskDelay(pdMS_TO_TICKS(2000));   /* let C6 finish booting before SDIO probe */

    if (lvgl_lock(200)) { ui_net_set_status("AT INIT..."); lvgl_unlock(); }

    bool ok = wifi_at_init(c6_en_ctrl, CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    if (!ok) {
        ESP_LOGW(TAG, "Primary WiFi failed, trying fallback '%s'", CONFIG_WIFI_SSID2);
        if (lvgl_lock(200)) { ui_net_set_status("TRYING FALLBACK..."); lvgl_unlock(); }
        ok = wifi_at_connect(CONFIG_WIFI_SSID2, CONFIG_WIFI_PASSWORD2);
    }
    if (!ok) {
        ESP_LOGE(TAG, "All WiFi networks failed");
        if (lvgl_lock(200)) { ui_net_set_status("WIFI FAIL"); lvgl_unlock(); }
        vTaskDelete(NULL);
        return;
    }

    /* Populate shared state so rest of code knows WiFi is up */
    s_wifi_up = true;
    wifi_at_get_ip((char *)s_device_ip, sizeof(s_device_ip));

    if (lvgl_lock(200)) {
        ui_net_set_status("WIFI OK");
        ui_set_wifi_ip((char *)s_device_ip);
        lvgl_unlock();
    }
    ESP_LOGI(TAG, "WiFi AT ready, IP: %s", (char *)s_device_ip);

    /* Sync system clock from C6 SNTP (blocking up to ~12 s) */
    time_t epoch = wifi_at_get_epoch();
    if (epoch > 0) {
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "System time synced via SNTP: %lld", (long long)epoch);
    }

    vTaskDelete(NULL);
}

static void network_init_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    xTaskCreate(c6_init_task, "c6_init", 16384, NULL, 3, NULL);
    eth_tester_init();
    // Touch reset runs here — after ETH init — to avoid any XL9535 IO3 interference
    touch_init();
    /* All LVGL API calls must be under the mutex — indev_create modifies
       LVGL internal lists that lv_timer_handler iterates concurrently. */
    if (lvgl_lock(2000)) {
        if (s_gt9895) {
            lv_indev_t *indev = lv_indev_create();
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, touch_read_cb);
            ESP_LOGI(TAG, "Touch indev registered");
        }
        ui_splash_done();
        lvgl_unlock();
        ESP_LOGI(TAG, "Splash dismissed");
    } else {
        ESP_LOGE(TAG, "LVGL lock timeout — forcing splash dismiss");
        ui_splash_done();
    }
    vTaskDelete(NULL);
}

// Runs LVGL + ui_init from a task with a large PSRAM stack
static void lvgl_init_task(void *arg) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)arg;

    size_t buf_size = LCD_H_RES * (LCD_V_RES / 10) * sizeof(uint16_t);
    void *lvgl_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lvgl_buf) lvgl_buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(lvgl_buf);

    /* Shadow framebuffer — lcd_flush_cb copies every partial render into this so
       screenshot_task can save it without ever touching the LVGL lock. */
    s_shadow_fb = heap_caps_calloc(LCD_H_RES * LCD_V_RES, sizeof(uint16_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_shadow_fb) ESP_LOGW(TAG, "Shadow FB alloc failed — screenshots disabled");

    lv_init();
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, lcd_flush_cb);
    lv_display_set_buffers(disp, lvgl_buf, NULL, buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Power the ES8311 analog supply before codec init */
    xl9535_p1(XL_VCCA_EN, true);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Audio codec init — non-fatal if hardware missing */
    if (audio_codec_init() != ESP_OK)
        ESP_LOGW(TAG, "Audio init failed — radio streaming unavailable");

    ui_set_flap_cb(flap_cb);
    ui_set_wifi_scan_cb(wifi_scan_cb);
    ui_set_wifi_connect_cb(wifi_connect_cb);
    ui_set_c6_flash_cb(wifi_reconnect_cb);
    ui_set_weather_radio_cb(weather_radio_cb);
    ui_set_weather_fetch_cb(weather_fetch_cb);
    ui_set_screenshot_cb(screenshot_cb);
    ui_set_cable_test_cb(cable_test_start_cb);
    ui_set_speed_test_cb(speed_test_start_cb);
    ui_set_locate_cb(locate_start_cb);
    ui_set_term_cmd_cb(term_cmd_cb);
    ui_init();

    /* 1-second clock timer — fires immediately then every second */
    esp_timer_handle_t clk_timer;
    esp_timer_create_args_t clk_args = {
        .callback = time_tick_cb,
        .name     = "clk",
    };
    esp_timer_create(&clk_args, &clk_timer);
    esp_timer_start_periodic(clk_timer, 1000000); /* 1 s */

    xTaskCreate(lvgl_tick_task, "lv_tick", 2048,  NULL, 5, NULL);
    xTaskCreate(lvgl_task,      "lv_task", 16384, NULL, 4, NULL);
    xTaskCreate(stats_task,     "stats",   6144,  NULL, 2, NULL);
    xTaskCreate(heartbeat_task, "hb",      4096,  NULL, 1, NULL);
    xTaskCreate(battery_task,    "batt",  8192, NULL, 1, NULL);
    /* 16 KB: esp_vfs_fat_sdmmc_mount alone needs ~8 KB deep in the call chain;
       combined with save_bmp + screenshot_task frames 8 KB is not enough. */
    xTaskCreate(screenshot_task, "shot", 16384, NULL, 1, NULL);
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

    /* Set timezone at boot so localtime_r is correct from the first tick. */
    setenv("TZ", CONFIG_TIMEZONE, 1);
    tzset();

    /* Seed RTC from compile timestamp so clock is roughly correct before NTP.
       NTP will overwrite this with the exact time once WiFi connects. */
    {
        struct tm bt = {0};
        static const char *months[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };
        char mon[4] = {0};
        sscanf(__DATE__, "%3s %d %d", mon, &bt.tm_mday, &bt.tm_year);
        bt.tm_year -= 1900;
        for (int i = 0; i < 12; i++) {
            if (strcmp(mon, months[i]) == 0) { bt.tm_mon = i; break; }
        }
        sscanf(__TIME__, "%d:%d:%d", &bt.tm_hour, &bt.tm_min, &bt.tm_sec);
        bt.tm_isdst = 1;  /* PDT is active May–Nov; mktime uses this for UTC conversion */
        time_t compile_t = mktime(&bt);
        struct timeval tv = { .tv_sec = compile_t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "RTC seeded from build time: %s %s", __DATE__, __TIME__);
    }

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
