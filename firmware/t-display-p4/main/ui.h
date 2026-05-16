#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#define LCD_H_RES  568
#define LCD_V_RES  1232
#define HDR_H      70

typedef void (*ui_brightness_cb_t)(int pct);
typedef void (*ui_term_cmd_cb_t)(const char *cmd, char *out, size_t out_len);
typedef void (*ui_wifi_connect_cb_t)(const char *ssid, const char *pass);
typedef void (*ui_wifi_scan_cb_t)(void);
typedef void (*ui_capture_cb_t)(void);
typedef void (*ui_cal_cb_t)(void);
typedef void (*ui_music_cmd_cb_t)(const char *cmd);
typedef void (*ui_bt_scan_cb_t)(void);
typedef void (*ui_weather_radio_cb_t)(bool start);
typedef void (*ui_flap_cb_t)(bool start, int interval_ms);
typedef void (*ui_screenshot_cb_t)(void);

typedef void (*ui_lora_send_cb_t)(const char *msg);
void ui_set_lora_send_cb(ui_lora_send_cb_t cb);
void ui_lora_set_status(const char *msg, bool ok);
void ui_lora_append_rx(const char *msg, int rssi, int snr);
void ui_lora_set_config(uint32_t freq_hz, int sf, int bw_khz, int power_dbm);

void ui_set_brightness_cb(ui_brightness_cb_t cb);
void ui_set_term_cmd_cb(ui_term_cmd_cb_t cb);
void ui_set_wifi_connect_cb(ui_wifi_connect_cb_t cb);
void ui_set_wifi_scan_cb(ui_wifi_scan_cb_t cb);
void ui_set_capture_cb(ui_capture_cb_t cb);
void ui_set_cal_cb(ui_cal_cb_t cb);
void ui_set_music_cmd_cb(ui_music_cmd_cb_t cb);
void ui_set_bt_scan_cb(ui_bt_scan_cb_t cb);
void ui_set_weather_radio_cb(ui_weather_radio_cb_t cb);
void ui_set_flap_cb(ui_flap_cb_t cb);
void ui_set_screenshot_cb(ui_screenshot_cb_t cb);

void ui_init(void);
void ui_splash_done(void);

void ui_set_status(const char *msg);
void ui_set_wifi_ip(const char *ip);
void ui_set_time(const char *t);
void ui_set_time_date(const char *d);
void ui_set_battery(int pct, bool charging);

void ui_set_eth_status(const char *msg);
void ui_set_eth_speed(int mbps, bool full_duplex);
void ui_set_eth_energy(bool detected);
void ui_set_eth_link(bool up);
void ui_set_eth_dhcp(const char *ip_str);
void ui_set_eth_net_info(const char *mac, const char *subnet, const char *gw, const char *ports, const char *vlan);
void ui_eth_log_append(const char *line);
void ui_eth_set_shot_status(const char *msg, bool ok);

void ui_term_append(const char *line);

void ui_net_set_status(const char *msg);
void ui_net_add_scan_result(const char *ssid, int rssi);
void ui_net_clear_scan(void);

void ui_gps_update(double lat, double lon, float alt, float speed_kmh, int sats, bool fix);

void ui_can_set_state(const char *state, bool active);
void ui_can_append_msg(const char *msg);

void ui_cam_set_frame(const void *rgb565, int w, int h);
void ui_cam_invalidate_cache(void);

void ui_music_set_track(const char *title, const char *artist, const char *album);
void ui_music_set_progress(int pos_ms, int dur_ms);
void ui_music_set_playing(bool playing);
void ui_music_set_connected(bool connected);

void ui_dash_set_stats(int heap_kb, int psram_kb, float cpu0, float cpu1);
void ui_dash_append_log(const char *line);

void ui_settings_set_ssid(const char *ssid);

void ui_weather_radio_append(const char *line);
void ui_weather_radio_set_status(const char *msg, bool active);

typedef void (*ui_cable_test_cb_t)(void);
void ui_set_cable_test_cb(ui_cable_test_cb_t cb);
void ui_cable_set_pair(int pair, const char *status, int len_m, bool ok);
void ui_cable_set_summary(bool pass, const char *mdi_str);

typedef void (*ui_locate_cb_t)(bool start);
void ui_set_locate_cb(ui_locate_cb_t cb);
