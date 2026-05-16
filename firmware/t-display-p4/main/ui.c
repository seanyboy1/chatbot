/*
 * ui.c — BLUE-NET OS  LVGL 9  T-Display P4  568×1232
 */

#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "UI";

#define W    LCD_H_RES
#define H    LCD_V_RES
#define HDR  HDR_H
#define CY   (H - HDR)

#define C_BG      lv_color_hex(0x0d0d0d)
#define C_CARD    lv_color_hex(0x141414)
#define C_PANEL   lv_color_hex(0x111111)
#define C_BORDER  lv_color_hex(0x1e1e1e)
#define C_ORANGE  lv_color_hex(0xFF5500)
#define C_FIRE    lv_color_hex(0xFF2200)
#define C_GREEN   lv_color_hex(0x00FF41)
#define C_BLUE    lv_color_hex(0x00BFFF)
#define C_RED     lv_color_hex(0xFF2222)
#define C_YELLOW  lv_color_hex(0xFFBD2E)
#define C_WHITE   lv_color_hex(0xCCCCCC)
#define C_DIM     lv_color_hex(0x444444)
#define C_DIMMER  lv_color_hex(0x1a1a1a)

/* ── Callbacks ───────────────────────────────────────────────────────────── */
static ui_brightness_cb_t    s_cb_bright;
static ui_term_cmd_cb_t      s_cb_term;
static ui_wifi_connect_cb_t  s_cb_wifi_con;
static ui_wifi_scan_cb_t     s_cb_wifi_scan;
static ui_capture_cb_t       s_cb_capture;
static ui_cal_cb_t           s_cb_cal;
static ui_music_cmd_cb_t     s_cb_music;
static ui_bt_scan_cb_t       s_cb_bt;
static ui_weather_radio_cb_t s_cb_wx;
static ui_flap_cb_t          s_cb_flap;
static ui_screenshot_cb_t    s_cb_ss;
static ui_lora_send_cb_t     s_cb_lora_send;
static ui_cable_test_cb_t    s_cb_cable_test;
static ui_locate_cb_t        s_cb_locate;

/* ── Screens ─────────────────────────────────────────────────────────────── */
static lv_obj_t *s_scr_splash;
static lv_obj_t *s_scr_home;
static lv_obj_t *s_scr_eth;
static lv_obj_t *s_scr_dash;
static lv_obj_t *s_scr_cam;
static lv_obj_t *s_scr_term;
static lv_obj_t *s_scr_settings;
static lv_obj_t *s_scr_net;
static lv_obj_t *s_scr_gps;
static lv_obj_t *s_scr_can;
static lv_obj_t *s_scr_music;

/* ── Home status bar ─────────────────────────────────────────────────────── */
static lv_obj_t *s_home_ip_lbl;
static lv_obj_t *s_home_status_lbl;

/* Global header label registry — updated by ui_set_time / ui_set_battery */
#define MAX_HDR_LBLS 14
static lv_obj_t *s_all_time_lbls[MAX_HDR_LBLS];
static lv_obj_t *s_all_date_lbls[MAX_HDR_LBLS];
static lv_obj_t *s_all_batt_lbls[MAX_HDR_LBLS];
static int       s_hdr_lbl_cnt = 0;

/* ── Splash ──────────────────────────────────────────────────────────────── */
static lv_obj_t *s_splash_status_lbl;

/* ── ETH screen ──────────────────────────────────────────────────────────── */
static lv_obj_t *s_eth_result_lbl;
static lv_obj_t *s_eth_result_sub_lbl;
static lv_obj_t *s_eth_status_lbl;
static lv_obj_t *s_eth_speed_lbl;
static lv_obj_t *s_eth_duplex_lbl;
static lv_obj_t *s_eth_link_dot;
static lv_obj_t *s_eth_energy_lbl;
static lv_obj_t *s_eth_dhcp_lbl;
static lv_obj_t *s_eth_mac_lbl;
static lv_obj_t *s_eth_subnet_lbl;
static lv_obj_t *s_eth_gw_lbl;
static lv_obj_t *s_eth_ports_lbl;
static lv_obj_t *s_eth_vlan_lbl;
static lv_obj_t *s_eth_conn_log;
static lv_obj_t *s_eth_conn_log_cont;
static lv_obj_t *s_eth_flap_btn_lbl;
static lv_obj_t *s_eth_locate_btn_lbl;
static lv_obj_t *s_eth_shot_lbl;
static int       s_flap_interval_ms = 1000;
static bool      s_flapping  = false;
static bool      s_locating  = false;

static lv_obj_t *s_cdt_pair_lbl[4];
static lv_obj_t *s_cdt_len_lbl[4];
static lv_obj_t *s_cdt_dot[4];
static lv_obj_t *s_cdt_result_lbl;
static lv_obj_t *s_cdt_mdi_lbl;

/* ── Terminal screen ─────────────────────────────────────────────────────── */
static lv_obj_t *s_term_log_lbl;
static lv_obj_t *s_term_log_cont;
static lv_obj_t *s_term_input_ta;
static char      s_term_buf[3072];

/* ── Network screen ──────────────────────────────────────────────────────── */
static lv_obj_t *s_net_status_lbl;
static lv_obj_t *s_net_scan_cont;
static lv_obj_t *s_net_ssid_ta;
static lv_obj_t *s_net_pass_ta;

/* ── GPS screen ──────────────────────────────────────────────────────────── */
static lv_obj_t *s_gps_fix_lbl;
static lv_obj_t *s_gps_lat_lbl;
static lv_obj_t *s_gps_lon_lbl;
static lv_obj_t *s_gps_alt_lbl;
static lv_obj_t *s_gps_spd_lbl;
static lv_obj_t *s_gps_sat_lbl;

/* ── CAN screen ──────────────────────────────────────────────────────────── */
static lv_obj_t *s_can_state_lbl;
static lv_obj_t *s_can_log_lbl;
static lv_obj_t *s_can_log_cont;
static char      s_can_buf[3072];

/* ── Camera screen ───────────────────────────────────────────────────────── */
static lv_obj_t *s_cam_status_lbl;
static bool      s_cam_dirty;

/* ── Dashboard screen ────────────────────────────────────────────────────── */
static lv_obj_t *s_dash_heap_lbl;
static lv_obj_t *s_dash_psram_lbl;
static lv_obj_t *s_dash_cpu0_lbl;
static lv_obj_t *s_dash_cpu1_lbl;
static lv_obj_t *s_dash_log_lbl;
static lv_obj_t *s_dash_log_cont;
static char      s_dash_log_buf[3072];

/* ── COMMS / music screen ────────────────────────────────────────────────── */
static lv_obj_t *s_comms_panels[4];
static lv_obj_t *s_comms_btns[4];
static int       s_comms_active;

/* Spotify panel */
static lv_obj_t *s_music_title_lbl;
static lv_obj_t *s_music_artist_lbl;
static lv_obj_t *s_music_album_lbl;
static lv_obj_t *s_music_conn_lbl;
static lv_obj_t *s_music_prog_bar;
static lv_obj_t *s_music_prog_lbl;

/* Weather panel */
static lv_obj_t *s_wx_log_lbl;
static lv_obj_t *s_wx_log_cont;
static lv_obj_t *s_wx_status_lbl;
static char      s_wx_buf[2048];

/* LoRa panel */
static lv_obj_t *s_lora_status_lbl;
static lv_obj_t *s_lora_config_lbl;
static lv_obj_t *s_lora_log_lbl;
static lv_obj_t *s_lora_log_cont;
static lv_obj_t *s_lora_tx_ta;
static char      s_lora_buf[2048];

/* ── Settings screen ─────────────────────────────────────────────────────── */
static lv_obj_t *s_settings_ssid_lbl;
static lv_obj_t *s_settings_bright_slider;

/* ── Shared keyboard ─────────────────────────────────────────────────────── */
static lv_obj_t *s_kb;

/* ═════════════════════════════════════════════════════════════════════════════
 *  Callback setters
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_set_brightness_cb(ui_brightness_cb_t cb)      { s_cb_bright     = cb; }
void ui_set_term_cmd_cb(ui_term_cmd_cb_t cb)           { s_cb_term       = cb; }
void ui_set_wifi_connect_cb(ui_wifi_connect_cb_t cb)   { s_cb_wifi_con   = cb; }
void ui_set_wifi_scan_cb(ui_wifi_scan_cb_t cb)         { s_cb_wifi_scan  = cb; }
void ui_set_capture_cb(ui_capture_cb_t cb)             { s_cb_capture    = cb; }
void ui_set_cal_cb(ui_cal_cb_t cb)                     { s_cb_cal        = cb; }
void ui_set_music_cmd_cb(ui_music_cmd_cb_t cb)         { s_cb_music      = cb; }
void ui_set_bt_scan_cb(ui_bt_scan_cb_t cb)             { s_cb_bt         = cb; }
void ui_set_weather_radio_cb(ui_weather_radio_cb_t cb) { s_cb_wx         = cb; }
void ui_set_flap_cb(ui_flap_cb_t cb)                   { s_cb_flap       = cb; }
void ui_set_screenshot_cb(ui_screenshot_cb_t cb)       { s_cb_ss         = cb; }
void ui_set_lora_send_cb(ui_lora_send_cb_t cb)         { s_cb_lora_send  = cb; }
void ui_set_cable_test_cb(ui_cable_test_cb_t cb)       { s_cb_cable_test = cb; }
void ui_set_locate_cb(ui_locate_cb_t cb)               { s_cb_locate     = cb; }

/* ═════════════════════════════════════════════════════════════════════════════
 *  Low-level helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void scr_bg(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, C_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t col) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text,
                           lv_color_t bg, lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, C_BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, C_WHITE, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
    return b;
}

/* Scrollable log area — returns the label inside */
static lv_obj_t *make_log_area(lv_obj_t *parent, int x, int y, int w, int h,
                                 lv_obj_t **cont_out) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_size(cont, w, h);
    lv_obj_set_style_bg_color(cont, C_DIMMER, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, C_BORDER, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 4, 0);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    if (cont_out) *cont_out = cont;

    lv_obj_t *l = lv_label_create(cont);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_color(l, C_GREEN, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_width(l, w - 20);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void log_append(lv_obj_t *lbl, lv_obj_t *cont,
                        const char *line, char *buf, int buf_sz) {
    int cur = (int)strlen(buf);
    int add = (int)strlen(line);
    if (cur + add + 2 >= buf_sz) {
        int half = buf_sz / 2;
        memmove(buf, buf + half, buf_sz - half);
        buf[buf_sz - half] = '\0';
        cur = (int)strlen(buf);
    }
    if (cur > 0) { buf[cur++] = '\n'; buf[cur] = '\0'; }
    strncat(buf, line, (size_t)(buf_sz - cur - 2));
    lv_label_set_text(lbl, buf);
    if (cont) lv_obj_scroll_to_y(cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* ── Header with optional back button ──────────────────────────────────────── */
static void back_cb(lv_event_t *e) {
    (void)e;
    lv_screen_load_anim(s_scr_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

static void swipe_back_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        lv_screen_load_anim(s_scr_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
}

static lv_obj_t *make_hdr(lv_obj_t *scr, const char *title, bool with_back) {
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, W, HDR);
    lv_obj_set_style_bg_color(hdr, C_PANEL, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, C_BORDER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Time (top-left, 2 lines) ── */
    lv_obj_t *tl = lv_label_create(hdr);
    lv_label_set_text(tl, "--:--");
    lv_obj_set_style_text_color(tl, C_WHITE, 0);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_20, 0);
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 70, 4);

    lv_obj_t *dl = lv_label_create(hdr);
    lv_label_set_text(dl, "NO SYNC");
    lv_obj_set_style_text_color(dl, C_BLUE, 0);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_12, 0);
    lv_obj_align(dl, LV_ALIGN_TOP_LEFT, 70, 30);

    /* ── Battery (top-right) ── */
    lv_obj_t *bl = lv_label_create(hdr);
    lv_label_set_text(bl, LV_SYMBOL_BATTERY_FULL " ?%");
    lv_obj_set_style_text_color(bl, C_YELLOW, 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_align(bl, LV_ALIGN_TOP_RIGHT, -70, 6);

    /* Register in global arrays */
    if (s_hdr_lbl_cnt < MAX_HDR_LBLS) {
        s_all_time_lbls[s_hdr_lbl_cnt] = tl;
        s_all_date_lbls[s_hdr_lbl_cnt] = dl;
        s_all_batt_lbls[s_hdr_lbl_cnt] = bl;
        s_hdr_lbl_cnt++;
    }

    if (with_back) {
        /* Title centered */
        lv_obj_t *ttl = lv_label_create(hdr);
        lv_label_set_text(ttl, title);
        lv_obj_set_style_text_color(ttl, C_ORANGE, 0);
        lv_obj_set_style_text_font(ttl, &lv_font_montserrat_14, 0);
        lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 8);

        /* Back button — bottom-left */
        lv_obj_t *btn = lv_btn_create(hdr);
        lv_obj_set_size(btn, 70, 28);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 8, -6);
        lv_obj_set_style_bg_color(btn, C_DIMMER, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, C_BORDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *bbl = lv_label_create(btn);
        lv_label_set_text(bbl, LV_SYMBOL_LEFT " HOME");
        lv_obj_set_style_text_color(bbl, C_WHITE, 0);
        lv_obj_set_style_text_font(bbl, &lv_font_montserrat_12, 0);
        lv_obj_center(bbl);
    } else {
        /* Home screen: title centered */
        lv_obj_t *ttl = lv_label_create(hdr);
        lv_label_set_text(ttl, title);
        lv_obj_set_style_text_color(ttl, C_ORANGE, 0);
        lv_obj_set_style_text_font(ttl, &lv_font_montserrat_16, 0);
        lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 6);
    }
    return hdr;
}

/* ── Keyboard focus callbacks ────────────────────────────────────────────── */
static void ta_focus_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (!s_kb) return;
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *make_ta(lv_obj_t *parent, const char *placeholder,
                          bool password, bool one_line) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_style_bg_color(ta, C_DIMMER, 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ta, C_BORDER, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 4, 0);
    lv_obj_set_style_text_color(ta, C_WHITE, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    if (password)    lv_textarea_set_password_mode(ta, true);
    if (one_line)    lv_textarea_set_one_line(ta, true);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_DEFOCUSED, NULL);
    return ta;
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  SPLASH SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void build_splash(void) {
    s_scr_splash = lv_obj_create(NULL);
    scr_bg(s_scr_splash);

    /* Big title */
    lv_obj_t *bn = lv_label_create(s_scr_splash);
    lv_label_set_text(bn, LV_SYMBOL_WIFI "  BLUE-NET OS");
    lv_obj_set_style_text_color(bn, C_ORANGE, 0);
    lv_obj_set_style_text_font(bn, &lv_font_montserrat_28, 0);
    lv_obj_align(bn, LV_ALIGN_CENTER, 0, -120);

    lv_obj_t *sub = lv_label_create(s_scr_splash);
    lv_label_set_text(sub, "T-DISPLAY P4  //  ESP32-P4  AMOLED 568×1232");
    lv_obj_set_style_text_color(sub, C_DIM, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -68);

    /* Divider line */
    lv_obj_t *line = lv_obj_create(s_scr_splash);
    lv_obj_set_pos(line, 40, H/2 - 30);
    lv_obj_set_size(line, W - 80, 1);
    lv_obj_set_style_bg_color(line, C_BORDER, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);

    /* Status pulsing label */
    s_splash_status_lbl = lv_label_create(s_scr_splash);
    lv_label_set_text(s_splash_status_lbl, "CONNECTING...");
    lv_obj_set_style_text_color(s_splash_status_lbl, C_GREEN, 0);
    lv_obj_set_style_text_font(s_splash_status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_splash_status_lbl, LV_ALIGN_CENTER, 0, 20);

    /* Version */
    lv_obj_t *ver = lv_label_create(s_scr_splash);
    lv_label_set_text(ver, "v2.0  //  BLUE-NET SYSTEMS");
    lv_obj_set_style_text_color(ver, C_DIMMER, 0);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -24);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  HOME SCREEN  — 2×4 tile grid
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Tile nav callback — user_data = target screen pointer */
static void tile_nav_cb(lv_event_t *e) {
    lv_obj_t *scr = (lv_obj_t *)lv_event_get_user_data(e);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

static void build_tile(lv_obj_t *parent, int col, int row,
                        const char *icon, const char *title,
                        const char *sub,  lv_color_t accent,
                        lv_obj_t *target_scr) {
    /* Tile geometry */
    const int GAP   = 8;
    const int TW    = (W - 3 * GAP) / 2;          /* 276 */
    const int TH    = (CY - 5 * GAP) / 4;          /* 283 */
    int x = GAP + col * (TW + GAP);
    int y = HDR + GAP + row * (TH + GAP);

    lv_obj_t *card = make_card(parent, x, y, TW, TH);
    if (target_scr) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, tile_nav_cb, LV_EVENT_CLICKED, target_scr);
    }

    /* Accent bar at top only */
    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, TW, 6);
    lv_obj_set_style_bg_color(bar, accent, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);

    /* Icon */
    lv_obj_t *ic = lv_label_create(card);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_color(ic, accent, 0);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -28);

    /* Title */
    lv_obj_t *ttl = lv_label_create(card);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_color(ttl, C_WHITE, 0);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_16, 0);
    lv_obj_align(ttl, LV_ALIGN_CENTER, 0, 20);

    /* Subtitle */
    lv_obj_t *st = lv_label_create(card);
    lv_label_set_text(st, sub);
    lv_obj_set_style_text_color(st, C_DIM, 0);
    lv_obj_set_style_text_font(st, &lv_font_montserrat_12, 0);
    lv_obj_align(st, LV_ALIGN_CENTER, 0, 46);
}

static void build_home(void) {
    s_scr_home = lv_obj_create(NULL);
    scr_bg(s_scr_home);

    /* Header — time/date/batt registered in global arrays via make_hdr */
    lv_obj_t *hdr = make_hdr(s_scr_home, LV_SYMBOL_WIFI "  BLUE-NET OS", false);

    /* Center of header: status + IP below the title */
    s_home_status_lbl = make_label(hdr, "CONNECTING...", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_home_status_lbl, LV_ALIGN_TOP_MID, 0, 28);

    s_home_ip_lbl = make_label(hdr, "---", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_home_ip_lbl, LV_ALIGN_TOP_MID, 0, 46);

    /* 2×4 tiles */
    build_tile(s_scr_home, 0, 0, LV_SYMBOL_GPS,    "GPS / NAV",   "L76K NMEA",    C_ORANGE, s_scr_gps);
    build_tile(s_scr_home, 1, 0, LV_SYMBOL_CHARGE, "DASHBOARD",   "SYSTEM STATS", C_ORANGE, s_scr_dash);
    build_tile(s_scr_home, 0, 1, LV_SYMBOL_IMAGE,  "CAMERA",      "CSI MIPI",     C_BLUE,   s_scr_cam);
    build_tile(s_scr_home, 1, 1, LV_SYMBOL_AUDIO,  "COMMS",       "LORA/BT/WX",   C_BLUE,   s_scr_music);
    build_tile(s_scr_home, 0, 2, LV_SYMBOL_LIST,   "TERMINAL",    "SHELL CMD",    C_BLUE,   s_scr_term);
    build_tile(s_scr_home, 1, 2, LV_SYMBOL_SHUFFLE,"CAN BUS",     "OBD-II 500K",  C_BLUE,   s_scr_can);
    build_tile(s_scr_home, 0, 3, LV_SYMBOL_USB,    "ETH TESTER",  "LAN8720 RMII", C_GREEN,  s_scr_eth);
    build_tile(s_scr_home, 1, 3, LV_SYMBOL_WIFI,   "NETWORK",     "WiFi / DHCP",  C_GREEN,  s_scr_net);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  ETH TESTER SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void screenshot_btn_cb(lv_event_t *e) { (void)e; if (s_cb_ss) s_cb_ss(); }
static void cable_test_btn_cb(lv_event_t *e) { (void)e; if (s_cb_cable_test) s_cb_cable_test(); }

static lv_obj_t *s_eth_flap_spd_btns[3];

static void flap_toggle_cb(lv_event_t *e) {
    s_flapping = !s_flapping;
    if (s_eth_flap_btn_lbl) {
        lv_label_set_text(s_eth_flap_btn_lbl,
            s_flapping ? LV_SYMBOL_STOP "  STOP FLAPPING" : LV_SYMBOL_PLAY "  START FLAPPING");
        lv_obj_set_style_text_color(s_eth_flap_btn_lbl, s_flapping ? C_WHITE : C_GREEN, 0);
    }
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_set_style_bg_color(btn, s_flapping ? C_FIRE : C_DIMMER, 0);
    lv_obj_set_style_border_color(btn, s_flapping ? C_FIRE : C_GREEN, 0);
    if (s_cb_flap) s_cb_flap(s_flapping, s_flap_interval_ms);
}

static void locate_toggle_cb(lv_event_t *e) {
    s_locating = !s_locating;
    if (s_eth_locate_btn_lbl) {
        lv_label_set_text(s_eth_locate_btn_lbl,
            s_locating ? LV_SYMBOL_STOP "  STOP LOCATE" : LV_SYMBOL_LOOP "  LOCATE PORT  (GREEN/AMBER)");
        lv_obj_set_style_text_color(s_eth_locate_btn_lbl, s_locating ? C_WHITE : C_GREEN, 0);
    }
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_set_style_bg_color(btn, s_locating ? C_FIRE : C_DIMMER, 0);
    lv_obj_set_style_border_color(btn, s_locating ? C_FIRE : C_GREEN, 0);
    if (s_cb_locate) s_cb_locate(s_locating);
}

static void flap_spd_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    static const int intervals[] = {200, 1000, 3000};
    s_flap_interval_ms = intervals[idx];
    for (int i = 0; i < 3; i++) {
        if (!s_eth_flap_spd_btns[i]) continue;
        lv_obj_set_style_bg_color(s_eth_flap_spd_btns[i],
            i == idx ? C_GREEN : C_DIMMER, 0);
    }
    if (s_flapping && s_cb_flap) s_cb_flap(true, s_flap_interval_ms);
}

static void build_eth(void) {
    s_scr_eth = lv_obj_create(NULL);
    scr_bg(s_scr_eth);
    lv_obj_add_flag(s_scr_eth, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_scr_eth, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_scr_eth, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(s_scr_eth, 20, 0);
    lv_obj_add_event_cb(s_scr_eth, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_t *eth_hdr = make_hdr(s_scr_eth, LV_SYMBOL_USB "  ETH CABLE TESTER", true);

    /* Screenshot button — bottom-right of header */
    lv_obj_t *ss_btn = lv_btn_create(eth_hdr);
    lv_obj_set_size(ss_btn, 80, 28);
    lv_obj_align(ss_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    lv_obj_set_style_bg_color(ss_btn, C_DIMMER, 0);
    lv_obj_set_style_bg_opa(ss_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ss_btn, C_BORDER, 0);
    lv_obj_set_style_border_width(ss_btn, 1, 0);
    lv_obj_set_style_radius(ss_btn, 4, 0);
    lv_obj_set_style_shadow_width(ss_btn, 0, 0);
    lv_obj_add_event_cb(ss_btn, screenshot_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ss_lbl = lv_label_create(ss_btn);
    lv_label_set_text(ss_lbl, LV_SYMBOL_IMAGE " SAVE");
    lv_obj_set_style_text_color(ss_lbl, C_WHITE, 0);
    lv_obj_set_style_text_font(ss_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(ss_lbl);

    /* Screenshot status — bottom-center of header */
    s_eth_shot_lbl = lv_label_create(eth_hdr);
    lv_label_set_text(s_eth_shot_lbl, "");
    lv_obj_set_style_text_color(s_eth_shot_lbl, C_DIM, 0);
    lv_obj_set_style_text_font(s_eth_shot_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_eth_shot_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* ── Cable test card — same style as flap card ── */
    lv_obj_t *ctc = make_card(s_scr_eth, 8, HDR + 818, W - 16, 280);
    make_label(ctc, "CABLE TEST", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(ctc, 0), LV_ALIGN_TOP_LEFT, 10, 8);

    /* Four pair rows inside the card */
    static const char *pair_names[] = {"PAIR 1,2", "PAIR 3,6", "PAIR 4,5", "PAIR 7,8"};
    int pair_y[] = { 28, 56, 84, 112 };
    for (int i = 0; i < 4; i++) {
        s_cdt_dot[i] = lv_obj_create(ctc);
        lv_obj_set_size(s_cdt_dot[i], 14, 14);
        lv_obj_set_pos(s_cdt_dot[i], 10, pair_y[i] + 2);
        lv_obj_set_style_bg_color(s_cdt_dot[i], C_DIM, 0);
        lv_obj_set_style_bg_opa(s_cdt_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_cdt_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_cdt_dot[i], 0, 0);

        lv_obj_t *pname = make_label(ctc, pair_names[i], &lv_font_montserrat_12, C_DIM);
        lv_obj_set_pos(pname, 32, pair_y[i]);

        s_cdt_pair_lbl[i] = make_label(ctc, "---", &lv_font_montserrat_12, C_WHITE);
        lv_obj_set_pos(s_cdt_pair_lbl[i], 170, pair_y[i]);

        s_cdt_len_lbl[i] = make_label(ctc, "", &lv_font_montserrat_12, C_DIM);
        lv_obj_set_pos(s_cdt_len_lbl[i], 340, pair_y[i]);
    }

    /* Summary: PASS/FAIL left, MDI type right */
    s_cdt_result_lbl = make_label(ctc, "---", &lv_font_montserrat_28, C_DIM);
    lv_obj_set_pos(s_cdt_result_lbl, 10, 140);

    s_cdt_mdi_lbl = make_label(ctc, "---", &lv_font_montserrat_14, C_DIM);
    lv_obj_align(s_cdt_mdi_lbl, LV_ALIGN_TOP_RIGHT, -10, 150);

    /* RUN TEST button — full width, same size as START FLAPPING */
    lv_obj_t *test_btn = make_btn(ctc, LV_SYMBOL_REFRESH "  RUN CABLE TEST",
                                  C_DIMMER, cable_test_btn_cb, NULL);
    lv_obj_set_style_border_color(test_btn, C_GREEN, 0);
    lv_obj_set_style_border_width(test_btn, 2, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(test_btn, 0), C_GREEN, 0);
    lv_obj_set_size(test_btn, W - 40, 68);
    lv_obj_align(test_btn, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* ── Three info cards ── */
    int cy = HDR + 8;
    int cw = 172, ch = 150;

    lv_obj_t *sc = make_card(s_scr_eth, 10, cy, cw, ch);
    make_label(sc, "SPEED", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(sc, 0), LV_ALIGN_TOP_MID, 0, 14);
    s_eth_speed_lbl = make_label(sc, "--", &lv_font_montserrat_24, C_DIM);
    lv_obj_align(s_eth_speed_lbl, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *dc = make_card(s_scr_eth, 198, cy, cw, ch);
    make_label(dc, "DUPLEX", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(dc, 0), LV_ALIGN_TOP_MID, 0, 14);
    s_eth_duplex_lbl = make_label(dc, "--", &lv_font_montserrat_20, C_DIM);
    lv_obj_align(s_eth_duplex_lbl, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *lc = make_card(s_scr_eth, 386, cy, cw, ch);
    make_label(lc, "LINK", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(lc, 0), LV_ALIGN_TOP_MID, 0, 14);
    s_eth_link_dot = lv_obj_create(lc);
    lv_obj_set_size(s_eth_link_dot, 40, 40);
    lv_obj_align(s_eth_link_dot, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_eth_link_dot, C_FIRE, 0);
    lv_obj_set_style_bg_opa(s_eth_link_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_eth_link_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_eth_link_dot, 0, 0);

    /* ── Energy ── */
    s_eth_energy_lbl = make_label(s_scr_eth, "ENERGY: NONE",
                                  &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_eth_energy_lbl, LV_ALIGN_TOP_LEFT, 14, HDR + 164);

    /* ── PHY caps card ── */
    lv_obj_t *pc = make_card(s_scr_eth, 8, HDR + 190, W - 16, 110);
    s_eth_status_lbl = make_label(pc,
        "CAPS: --\nPOLARITY: NORMAL",
        &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_eth_status_lbl, LV_ALIGN_TOP_LEFT, 10, 12);

    /* ── Network info card ── */
    lv_obj_t *nc = make_card(s_scr_eth, 8, HDR + 310, W - 16, 310);
    make_label(nc, "NETWORK", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(nc, 0), LV_ALIGN_TOP_LEFT, 10, 8);

    /* key labels (small/dim) + value labels (larger/bright) */
    #define NET_ROW(yoff, key, val_ptr, val_init, col) \
        make_label(nc, key, &lv_font_montserrat_12, C_DIM); \
        lv_obj_align(lv_obj_get_child(nc, -1), LV_ALIGN_TOP_LEFT, 10, yoff); \
        val_ptr = make_label(nc, val_init, &lv_font_montserrat_16, col); \
        lv_obj_align(val_ptr, LV_ALIGN_TOP_LEFT, 130, yoff - 2);

    NET_ROW(30,  "IP:",      s_eth_dhcp_lbl,   "waiting...",        C_DIM)
    NET_ROW(58,  "MAC:",     s_eth_mac_lbl,    "--:--:--:--:--:--", C_DIM)
    NET_ROW(86,  "SUBNET:",  s_eth_subnet_lbl, "---",               C_DIM)
    NET_ROW(114, "GATEWAY:", s_eth_gw_lbl,     "---",               C_DIM)
    NET_ROW(142, "PORTS:",   s_eth_ports_lbl,  "---",               C_DIM)
    NET_ROW(170, "VLAN ID:", s_eth_vlan_lbl,   "none",              C_DIM)
    #undef NET_ROW

    /* ── Connection log card ── */
    lv_obj_t *logc = make_card(s_scr_eth, 8, HDR + 630, W - 16, 180);
    make_label(logc, "CONNECTION LOG", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(logc, 0), LV_ALIGN_TOP_LEFT, 10, 8);

    s_eth_conn_log_cont = lv_obj_create(logc);
    lv_obj_set_pos(s_eth_conn_log_cont, 8, 26);
    lv_obj_set_size(s_eth_conn_log_cont, W - 36, 142);
    lv_obj_set_style_bg_color(s_eth_conn_log_cont, C_DIMMER, 0);
    lv_obj_set_style_bg_opa(s_eth_conn_log_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_eth_conn_log_cont, 0, 0);
    lv_obj_set_style_pad_all(s_eth_conn_log_cont, 4, 0);
    lv_obj_set_style_radius(s_eth_conn_log_cont, 4, 0);
    lv_obj_set_flex_flow(s_eth_conn_log_cont, LV_FLEX_FLOW_COLUMN);

    s_eth_conn_log = lv_label_create(s_eth_conn_log_cont);
    lv_label_set_text(s_eth_conn_log, "");
    lv_obj_set_width(s_eth_conn_log, W - 44);
    lv_obj_set_style_text_color(s_eth_conn_log, C_GREEN, 0);
    lv_obj_set_style_text_font(s_eth_conn_log, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_eth_conn_log, LV_LABEL_LONG_WRAP);

    /* ── Flap section ── */
    lv_obj_t *fc = make_card(s_scr_eth, 8, HDR + 1106, W - 16, 252);

    /* Speed selector row */
    make_label(fc, "FLAP SPEED", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(fc, 0), LV_ALIGN_TOP_LEFT, 10, 8);

    const char *spd_labels[] = {"FAST", "MED", "SLOW"};
    int bw = (W - 16 - 40) / 3;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *sb = make_btn(fc, spd_labels[i], i == 1 ? C_DIMMER : (i == 0 ? C_GREEN : C_DIMMER),
                                flap_spd_cb, (void *)(intptr_t)i);
        lv_obj_set_size(sb, bw, 44);
        lv_obj_set_pos(sb, 8 + i * (bw + 4), 28);
        s_eth_flap_spd_btns[i] = sb;
    }

    /* Start/Stop flap button */
    lv_obj_t *fb = make_btn(fc, LV_SYMBOL_PLAY "  START FLAPPING",
                            C_DIMMER, flap_toggle_cb, NULL);
    lv_obj_set_style_border_color(fb, C_GREEN, 0);
    lv_obj_set_style_border_width(fb, 2, 0);
    lv_obj_set_size(fb, W - 40, 68);
    lv_obj_set_pos(fb, 8, 80);
    s_eth_flap_btn_lbl = lv_obj_get_child(fb, 0);
    lv_obj_set_style_text_color(s_eth_flap_btn_lbl, C_GREEN, 0);

    /* Locate port button */
    lv_obj_t *lb = make_btn(fc, LV_SYMBOL_LOOP "  LOCATE PORT  (GREEN/AMBER)",
                            C_DIMMER, locate_toggle_cb, NULL);
    lv_obj_set_style_border_color(lb, C_GREEN, 0);
    lv_obj_set_style_border_width(lb, 2, 0);
    lv_obj_set_size(lb, W - 40, 68);
    lv_obj_set_pos(lb, 8, 156);
    s_eth_locate_btn_lbl = lv_obj_get_child(lb, 0);
    lv_obj_set_style_text_color(s_eth_locate_btn_lbl, C_GREEN, 0);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  DASHBOARD SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void build_dash(void) {
    s_scr_dash = lv_obj_create(NULL);
    scr_bg(s_scr_dash);
    lv_obj_add_event_cb(s_scr_dash, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_dash, LV_SYMBOL_CHARGE "  DASHBOARD", true);

    /* 2×2 stat cards */
    const int CW = 260, CH = 120, GAP = 8;
    int y0 = HDR + GAP;

    /* HEAP */
    lv_obj_t *hc = make_card(s_scr_dash, GAP, y0, CW, CH);
    make_label(hc, "FREE HEAP", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(hc,0), LV_ALIGN_TOP_MID, 0, 14);
    s_dash_heap_lbl = make_label(hc, "---", &lv_font_montserrat_24, C_GREEN);
    lv_obj_align(s_dash_heap_lbl, LV_ALIGN_CENTER, 0, 10);

    /* PSRAM */
    lv_obj_t *pc = make_card(s_scr_dash, GAP*2+CW, y0, CW, CH);
    make_label(pc, "PSRAM FREE", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(pc,0), LV_ALIGN_TOP_MID, 0, 14);
    s_dash_psram_lbl = make_label(pc, "---", &lv_font_montserrat_24, C_BLUE);
    lv_obj_align(s_dash_psram_lbl, LV_ALIGN_CENTER, 0, 10);

    int y1 = y0 + CH + GAP;

    /* CPU0 */
    lv_obj_t *c0 = make_card(s_scr_dash, GAP, y1, CW, CH);
    make_label(c0, "CPU0 LOAD", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(c0,0), LV_ALIGN_TOP_MID, 0, 14);
    s_dash_cpu0_lbl = make_label(c0, "0%", &lv_font_montserrat_24, C_ORANGE);
    lv_obj_align(s_dash_cpu0_lbl, LV_ALIGN_CENTER, 0, 10);

    /* CPU1 */
    lv_obj_t *c1 = make_card(s_scr_dash, GAP*2+CW, y1, CW, CH);
    make_label(c1, "CPU1 LOAD", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(c1,0), LV_ALIGN_TOP_MID, 0, 14);
    s_dash_cpu1_lbl = make_label(c1, "0%", &lv_font_montserrat_24, C_ORANGE);
    lv_obj_align(s_dash_cpu1_lbl, LV_ALIGN_CENTER, 0, 10);

    /* Log area fills remaining space */
    int log_y = y1 + CH + GAP;
    int log_h = H - log_y - GAP;
    s_dash_log_lbl = make_log_area(s_scr_dash, GAP, log_y,
                                    W - 2*GAP, log_h, &s_dash_log_cont);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  CAMERA SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void capture_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_cb_capture) s_cb_capture();
}
static void cal_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_cb_cal) s_cb_cal();
}

static void build_cam(void) {
    s_scr_cam = lv_obj_create(NULL);
    scr_bg(s_scr_cam);
    lv_obj_add_event_cb(s_scr_cam, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_cam, LV_SYMBOL_IMAGE "  CAMERA", true);

    /* Placeholder panel */
    lv_obj_t *cam_panel = make_card(s_scr_cam, 8, HDR + 8, W - 16, CY - 80);
    lv_obj_set_style_border_color(cam_panel, C_DIMMER, 0);

    s_cam_status_lbl = make_label(cam_panel, LV_SYMBOL_IMAGE "\nCAMERA INITIALIZING...",
                                  &lv_font_montserrat_16, C_DIM);
    lv_label_set_long_mode(s_cam_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_cam_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_cam_status_lbl, LV_ALIGN_CENTER, 0, 0);

    /* Button row */
    int by = HDR + CY - 68;
    lv_obj_t *cap = make_btn(s_scr_cam, LV_SYMBOL_IMAGE " CAPTURE",
                             C_ORANGE, capture_btn_cb, NULL);
    lv_obj_set_size(cap, 250, 56);
    lv_obj_set_pos(cap, 8, by);

    lv_obj_t *cal = make_btn(s_scr_cam, LV_SYMBOL_SETTINGS " CALIBRATE",
                             C_DIMMER, cal_btn_cb, NULL);
    lv_obj_set_size(cal, 250, 56);
    lv_obj_set_pos(cal, W - 258, by);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  TERMINAL SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void term_send_cb(lv_event_t *e) {
    (void)e;
    if (!s_term_input_ta) return;
    const char *cmd = lv_textarea_get_text(s_term_input_ta);
    if (!cmd || cmd[0] == '\0') return;

    char echo[256];
    snprintf(echo, sizeof(echo), "> %s", cmd);
    log_append(s_term_log_lbl, s_term_log_cont, echo, s_term_buf, sizeof(s_term_buf));

    if (s_cb_term) {
        char out[512] = {0};
        s_cb_term(cmd, out, sizeof(out));
        /* output is appended inside the callback via ui_term_append */
    }
    lv_textarea_set_text(s_term_input_ta, "");
}

static void build_term(void) {
    s_scr_term = lv_obj_create(NULL);
    scr_bg(s_scr_term);
    lv_obj_add_event_cb(s_scr_term, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_term, LV_SYMBOL_LIST "  TERMINAL", true);

    /* Log area */
    int log_h = CY - 80;
    s_term_log_lbl = make_log_area(s_scr_term, 8, HDR + 8,
                                    W - 16, log_h, &s_term_log_cont);
    lv_obj_set_style_text_color(s_term_log_lbl, C_GREEN, 0);

    /* Input row */
    int iy = HDR + log_h + 16;
    s_term_input_ta = make_ta(s_scr_term, "type command...", false, true);
    lv_obj_set_pos(s_term_input_ta, 8, iy);
    lv_obj_set_size(s_term_input_ta, W - 100, 52);

    lv_obj_t *send = make_btn(s_scr_term, LV_SYMBOL_RIGHT " SEND",
                              C_ORANGE, term_send_cb, NULL);
    lv_obj_set_size(send, 80, 52);
    lv_obj_set_pos(send, W - 86, iy);

    /* Welcome message */
    log_append(s_term_log_lbl, s_term_log_cont,
               "BLUE-NET OS Shell v2.0  (type 'help')",
               s_term_buf, sizeof(s_term_buf));
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  NETWORK SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void net_scan_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_cb_wifi_scan) s_cb_wifi_scan();
}

static void net_connect_btn_cb(lv_event_t *e) {
    (void)e;
    if (!s_net_ssid_ta || !s_net_pass_ta) return;
    const char *ssid = lv_textarea_get_text(s_net_ssid_ta);
    const char *pass = lv_textarea_get_text(s_net_pass_ta);
    if (s_cb_wifi_con && ssid && ssid[0]) {
        s_cb_wifi_con(ssid, pass ? pass : "");
    }
}

static void build_net(void) {
    s_scr_net = lv_obj_create(NULL);
    scr_bg(s_scr_net);
    lv_obj_add_event_cb(s_scr_net, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_net, LV_SYMBOL_WIFI "  NETWORK", true);

    int y = HDR + 12;

    s_net_status_lbl = make_label(s_scr_net, "STATUS: --",
                                  &lv_font_montserrat_14, C_DIM);
    lv_obj_set_pos(s_net_status_lbl, 12, y);
    y += 44;

    /* SSID input */
    make_label(s_scr_net, "SSID:", &lv_font_montserrat_12, C_DIM);
    lv_obj_set_pos(lv_obj_get_child(s_scr_net, lv_obj_get_child_cnt(s_scr_net)-1), 12, y);
    y += 22;
    s_net_ssid_ta = make_ta(s_scr_net, "Network name", false, true);
    lv_obj_set_pos(s_net_ssid_ta, 8, y);
    lv_obj_set_size(s_net_ssid_ta, W - 16, 52);
    y += 64;

    /* Password input */
    make_label(s_scr_net, "PASSWORD:", &lv_font_montserrat_12, C_DIM);
    lv_obj_set_pos(lv_obj_get_child(s_scr_net, lv_obj_get_child_cnt(s_scr_net)-1), 12, y);
    y += 22;
    s_net_pass_ta = make_ta(s_scr_net, "Password", true, true);
    lv_obj_set_pos(s_net_pass_ta, 8, y);
    lv_obj_set_size(s_net_pass_ta, W - 16, 52);
    y += 72;

    /* Buttons */
    lv_obj_t *con = make_btn(s_scr_net, LV_SYMBOL_WIFI " CONNECT",
                             C_ORANGE, net_connect_btn_cb, NULL);
    lv_obj_set_size(con, 260, 56);
    lv_obj_set_pos(con, 8, y);

    lv_obj_t *scn = make_btn(s_scr_net, LV_SYMBOL_REFRESH " SCAN",
                             C_DIMMER, net_scan_btn_cb, NULL);
    lv_obj_set_size(scn, 260, 56);
    lv_obj_set_pos(scn, W - 268, y);
    y += 72;

    /* Scan results */
    make_label(s_scr_net, "SCAN RESULTS:", &lv_font_montserrat_12, C_DIM);
    lv_obj_set_pos(lv_obj_get_child(s_scr_net, lv_obj_get_child_cnt(s_scr_net)-1), 12, y);
    y += 22;

    s_net_scan_cont = lv_obj_create(s_scr_net);
    lv_obj_set_pos(s_net_scan_cont, 8, y);
    lv_obj_set_size(s_net_scan_cont, W - 16, H - y - 8);
    lv_obj_set_style_bg_color(s_net_scan_cont, C_DIMMER, 0);
    lv_obj_set_style_bg_opa(s_net_scan_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_net_scan_cont, C_BORDER, 0);
    lv_obj_set_style_border_width(s_net_scan_cont, 1, 0);
    lv_obj_set_style_radius(s_net_scan_cont, 4, 0);
    lv_obj_set_style_pad_all(s_net_scan_cont, 6, 0);
    lv_obj_set_flex_flow(s_net_scan_cont, LV_FLEX_FLOW_COLUMN);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  GPS SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void build_gps(void) {
    s_scr_gps = lv_obj_create(NULL);
    scr_bg(s_scr_gps);
    lv_obj_add_event_cb(s_scr_gps, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_gps, LV_SYMBOL_GPS "  GPS / NAV", true);

    /* Big FIX indicator */
    s_gps_fix_lbl = make_label(s_scr_gps, "NO FIX",
                               &lv_font_montserrat_48, C_RED);
    lv_obj_align(s_gps_fix_lbl, LV_ALIGN_TOP_MID, 0, HDR + 32);

    /* Divider */
    lv_obj_t *div = lv_obj_create(s_scr_gps);
    lv_obj_set_pos(div, 20, HDR + 110);
    lv_obj_set_size(div, W - 40, 1);
    lv_obj_set_style_bg_color(div, C_BORDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    /* Data cards */
    int cw = (W - 3*8) / 2;
    int ch = 120;
    int y  = HDR + 130;

    lv_obj_t *latc = make_card(s_scr_gps, 8, y, cw, ch);
    make_label(latc, "LATITUDE", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(latc,0), LV_ALIGN_TOP_MID, 0, 12);
    s_gps_lat_lbl = make_label(latc, "---.------", &lv_font_montserrat_20, C_WHITE);
    lv_obj_align(s_gps_lat_lbl, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *lonc = make_card(s_scr_gps, 16+cw, y, cw, ch);
    make_label(lonc, "LONGITUDE", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(lonc,0), LV_ALIGN_TOP_MID, 0, 12);
    s_gps_lon_lbl = make_label(lonc, "---.------", &lv_font_montserrat_20, C_WHITE);
    lv_obj_align(s_gps_lon_lbl, LV_ALIGN_CENTER, 0, 10);

    y += ch + 8;
    int cw3 = (W - 4*8) / 3;

    lv_obj_t *altc = make_card(s_scr_gps, 8, y, cw3, ch);
    make_label(altc, "ALTITUDE", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(altc,0), LV_ALIGN_TOP_MID, 0, 12);
    s_gps_alt_lbl = make_label(altc, "--m", &lv_font_montserrat_20, C_WHITE);
    lv_obj_align(s_gps_alt_lbl, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *spdc = make_card(s_scr_gps, 16+cw3, y, cw3, ch);
    make_label(spdc, "SPEED", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(spdc,0), LV_ALIGN_TOP_MID, 0, 12);
    s_gps_spd_lbl = make_label(spdc, "--km/h", &lv_font_montserrat_20, C_WHITE);
    lv_obj_align(s_gps_spd_lbl, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *satc = make_card(s_scr_gps, 24+2*cw3, y, cw3, ch);
    make_label(satc, "SATELLITES", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(satc,0), LV_ALIGN_TOP_MID, 0, 12);
    s_gps_sat_lbl = make_label(satc, "0", &lv_font_montserrat_24, C_WHITE);
    lv_obj_align(s_gps_sat_lbl, LV_ALIGN_CENTER, 0, 10);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  CAN BUS SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void build_can(void) {
    s_scr_can = lv_obj_create(NULL);
    scr_bg(s_scr_can);
    lv_obj_add_event_cb(s_scr_can, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_can, LV_SYMBOL_SHUFFLE "  CAN BUS", true);

    s_can_state_lbl = make_label(s_scr_can, "INIT...",
                                 &lv_font_montserrat_16, C_DIM);
    lv_obj_align(s_can_state_lbl, LV_ALIGN_TOP_MID, 0, HDR + 16);

    int log_y = HDR + 60;
    s_can_log_lbl = make_log_area(s_scr_can, 8, log_y,
                                   W - 16, H - log_y - 8, &s_can_log_cont);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  SETTINGS SCREEN
 * ═══════════════════════════════════════════════════════════════════════════ */
static void bright_cb(lv_event_t *e) {
    (void)e;
    if (!s_settings_bright_slider) return;
    int val = lv_slider_get_value(s_settings_bright_slider);
    if (s_cb_bright) s_cb_bright(val);
}

static void bt_scan_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_cb_bt) s_cb_bt();
}

static void build_settings(void) {
    s_scr_settings = lv_obj_create(NULL);
    scr_bg(s_scr_settings);
    lv_obj_add_event_cb(s_scr_settings, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_settings, LV_SYMBOL_SETTINGS "  SETTINGS", true);

    int y = HDR + 24;

    /* Brightness */
    make_label(s_scr_settings, "DISPLAY BRIGHTNESS", &lv_font_montserrat_14, C_WHITE);
    lv_obj_set_pos(lv_obj_get_child(s_scr_settings, lv_obj_get_child_cnt(s_scr_settings)-1),
                   12, y);
    y += 32;

    s_settings_bright_slider = lv_slider_create(s_scr_settings);
    lv_slider_set_range(s_settings_bright_slider, 0, 100);
    lv_slider_set_value(s_settings_bright_slider, 80, LV_ANIM_OFF);
    lv_obj_set_pos(s_settings_bright_slider, 12, y);
    lv_obj_set_size(s_settings_bright_slider, W - 24, 32);
    lv_obj_set_style_bg_color(s_settings_bright_slider, C_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_settings_bright_slider, C_ORANGE, LV_PART_KNOB);
    lv_obj_add_event_cb(s_settings_bright_slider, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    y += 52;

    /* Divider */
    lv_obj_t *d1 = lv_obj_create(s_scr_settings);
    lv_obj_set_pos(d1, 12, y); lv_obj_set_size(d1, W-24, 1);
    lv_obj_set_style_bg_color(d1, C_BORDER, 0);
    lv_obj_set_style_bg_opa(d1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d1, 0, 0);
    y += 16;

    /* WiFi SSID */
    make_label(s_scr_settings, "SAVED WIFI SSID", &lv_font_montserrat_14, C_WHITE);
    lv_obj_set_pos(lv_obj_get_child(s_scr_settings, lv_obj_get_child_cnt(s_scr_settings)-1),
                   12, y);
    y += 32;
    s_settings_ssid_lbl = make_label(s_scr_settings, "(none)",
                                     &lv_font_montserrat_16, C_DIM);
    lv_obj_set_pos(s_settings_ssid_lbl, 16, y);
    y += 44;

    /* BT scan */
    lv_obj_t *bt = make_btn(s_scr_settings, LV_SYMBOL_BLUETOOTH " BT DEVICE SCAN",
                            C_BLUE, bt_scan_btn_cb, NULL);
    lv_obj_set_size(bt, W - 24, 56);
    lv_obj_set_pos(bt, 12, y);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  COMMS SCREEN  (Spotify / Weather / Broadcast / LoRa)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void comms_tab_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_comms_active = idx;
    for (int i = 0; i < 4; i++) {
        if (i == idx) {
            lv_obj_clear_flag(s_comms_panels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_comms_btns[i], C_ORANGE, 0);
            lv_obj_set_style_bg_color(s_comms_btns[i], C_ORANGE, LV_PART_MAIN);
        } else {
            lv_obj_add_flag(s_comms_panels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_comms_btns[i], C_DIMMER, 0);
            lv_obj_set_style_bg_color(s_comms_btns[i], C_DIMMER, LV_PART_MAIN);
        }
    }
}

static void music_prev_cb(lv_event_t *e) { (void)e; if (s_cb_music) s_cb_music("previous"); }
static void music_play_cb(lv_event_t *e) { (void)e; if (s_cb_music) s_cb_music("play-pause"); }
static void music_next_cb(lv_event_t *e) { (void)e; if (s_cb_music) s_cb_music("next"); }

static void lora_send_btn_cb(lv_event_t *e) {
    (void)e;
    if (!s_lora_tx_ta) return;
    const char *msg = lv_textarea_get_text(s_lora_tx_ta);
    if (!msg || msg[0] == '\0') return;
    if (s_cb_lora_send) s_cb_lora_send(msg);
    char echo[160];
    snprintf(echo, sizeof(echo), "TX: %s", msg);
    log_append(s_lora_log_lbl, s_lora_log_cont, echo, s_lora_buf, sizeof(s_lora_buf));
    lv_textarea_set_text(s_lora_tx_ta, "");
}

static void wx_start_btn_cb(lv_event_t *e) {
    (void)e;
    if (s_cb_wx) s_cb_wx(true);
}

static void build_comms_spotify(lv_obj_t *parent) {
    /* Connected status */
    s_music_conn_lbl = make_label(parent, "NOT CONNECTED",
                                  &lv_font_montserrat_14, C_DIM);
    lv_obj_align(s_music_conn_lbl, LV_ALIGN_TOP_MID, 0, 20);

    /* Track info */
    s_music_title_lbl = make_label(parent, "---", &lv_font_montserrat_24, C_WHITE);
    lv_obj_set_width(s_music_title_lbl, W - 32);
    lv_label_set_long_mode(s_music_title_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_music_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_music_title_lbl, LV_ALIGN_TOP_MID, 0, 60);

    s_music_artist_lbl = make_label(parent, "---", &lv_font_montserrat_16, C_DIM);
    lv_obj_set_style_text_align(s_music_artist_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_music_artist_lbl, LV_ALIGN_TOP_MID, 0, 100);

    s_music_album_lbl = make_label(parent, "", &lv_font_montserrat_12, C_DIM);
    lv_obj_set_style_text_align(s_music_album_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_music_album_lbl, LV_ALIGN_TOP_MID, 0, 128);

    /* Progress bar */
    s_music_prog_bar = lv_bar_create(parent);
    lv_obj_set_size(s_music_prog_bar, W - 40, 8);
    lv_obj_align(s_music_prog_bar, LV_ALIGN_TOP_MID, 0, 160);
    lv_bar_set_range(s_music_prog_bar, 0, 1000);
    lv_bar_set_value(s_music_prog_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_music_prog_bar, C_DIMMER, 0);
    lv_obj_set_style_bg_color(s_music_prog_bar, C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_music_prog_bar, 4, 0);
    lv_obj_set_style_radius(s_music_prog_bar, 4, LV_PART_INDICATOR);

    s_music_prog_lbl = make_label(parent, "0:00 / 0:00",
                                  &lv_font_montserrat_12, C_DIM);
    lv_obj_set_style_text_align(s_music_prog_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_music_prog_lbl, LV_ALIGN_TOP_MID, 0, 178);

    /* Control buttons */
    int by = 210;
    int bw = (W - 48) / 3;
    lv_obj_t *prev = make_btn(parent, LV_SYMBOL_PREV, C_DIMMER, music_prev_cb, NULL);
    lv_obj_set_size(prev, bw, 64);
    lv_obj_set_pos(prev, 16, by);

    lv_obj_t *play = make_btn(parent, LV_SYMBOL_PLAY, C_ORANGE, music_play_cb, NULL);
    lv_obj_set_size(play, bw, 64);
    lv_obj_set_pos(play, 16 + bw + 8, by);

    lv_obj_t *next = make_btn(parent, LV_SYMBOL_NEXT, C_DIMMER, music_next_cb, NULL);
    lv_obj_set_size(next, bw, 64);
    lv_obj_set_pos(next, 16 + 2*(bw + 8), by);
}

static void build_comms_weather(lv_obj_t *parent) {
    s_wx_status_lbl = make_label(parent, "WEATHER RADIO IDLE",
                                 &lv_font_montserrat_14, C_DIM);
    lv_obj_align(s_wx_status_lbl, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *wb = make_btn(parent, LV_SYMBOL_REFRESH " FETCH WEATHER",
                            C_BLUE, wx_start_btn_cb, NULL);
    lv_obj_set_size(wb, W - 32, 52);
    lv_obj_align(wb, LV_ALIGN_TOP_MID, 0, 48);

    int log_y = 112;
    s_wx_log_lbl = make_log_area(parent, 8, log_y,
                                  W - 16, 400, &s_wx_log_cont);
}

static void build_comms_broadcast(lv_obj_t *parent) {
    make_label(parent, "MESH BROADCAST", &lv_font_montserrat_16, C_WHITE);
    lv_obj_align(lv_obj_get_child(parent, 0), LV_ALIGN_TOP_MID, 0, 20);

    make_label(parent, "Send a message to all mesh nodes",
               &lv_font_montserrat_12, C_DIM);
    lv_obj_align(lv_obj_get_child(parent, 1), LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *bcast_ta = make_ta(parent, "Broadcast message...", false, false);
    lv_obj_set_pos(bcast_ta, 8, 80);
    lv_obj_set_size(bcast_ta, W - 16, 160);

    lv_obj_t *sb = make_btn(parent, LV_SYMBOL_UPLOAD " BROADCAST",
                            C_ORANGE, NULL, NULL);
    lv_obj_set_size(sb, W - 16, 56);
    lv_obj_set_pos(sb, 8, 252);
}

static void build_comms_lora(lv_obj_t *parent) {
    /* Status */
    s_lora_status_lbl = make_label(parent, LV_SYMBOL_REFRESH "  LORA INIT...",
                                   &lv_font_montserrat_14, C_DIM);
    lv_obj_align(s_lora_status_lbl, LV_ALIGN_TOP_MID, 0, 12);

    /* Config */
    s_lora_config_lbl = make_label(parent, "915MHz  SF7  BW125  +14dBm",
                                   &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_lora_config_lbl, LV_ALIGN_TOP_MID, 0, 40);

    /* RX log */
    int log_h = 340;
    s_lora_log_lbl = make_log_area(parent, 8, 68,
                                    W - 16, log_h, &s_lora_log_cont);

    /* TX row */
    int ty = 68 + log_h + 8;
    s_lora_tx_ta = make_ta(parent, "Type message to transmit...", false, true);
    lv_obj_set_pos(s_lora_tx_ta, 8, ty);
    lv_obj_set_size(s_lora_tx_ta, W - 108, 52);

    lv_obj_t *sb = make_btn(parent, LV_SYMBOL_UP " TX",
                            C_ORANGE, lora_send_btn_cb, NULL);
    lv_obj_set_size(sb, 90, 52);
    lv_obj_set_pos(sb, W - 98, ty);
}

static void build_music(void) {
    s_scr_music = lv_obj_create(NULL);
    scr_bg(s_scr_music);
    lv_obj_add_event_cb(s_scr_music, swipe_back_cb, LV_EVENT_GESTURE, NULL);
    make_hdr(s_scr_music, LV_SYMBOL_AUDIO "  COMMS", true);

    /* Tab button row */
    const char *tab_names[] = {
        LV_SYMBOL_AUDIO " SPOTIFY",
        LV_SYMBOL_REFRESH " WEATHER",
        LV_SYMBOL_UPLOAD " BROADCAST",
        LV_SYMBOL_BARS " LORA"
    };
    int tab_y   = HDR + 4;
    int tab_h   = 40;
    int tab_w   = W / 4;
    for (int i = 0; i < 4; i++) {
        lv_color_t bg = (i == 0) ? C_ORANGE : C_DIMMER;
        lv_obj_t *btn = make_btn(s_scr_music, tab_names[i],
                                 bg, comms_tab_cb, (void *)(intptr_t)i);
        lv_obj_set_size(btn, tab_w - 4, tab_h);
        lv_obj_set_pos(btn, i * tab_w + 2, tab_y);
        lv_obj_set_style_radius(btn, 4, 0);
        /* Make text smaller to fit */
        lv_obj_set_style_text_font(lv_obj_get_child(btn, 0),
                                   &lv_font_montserrat_12, 0);
        s_comms_btns[i] = btn;
    }

    /* Content panels (stacked, only one visible at a time) */
    int panel_y = HDR + tab_h + 8;
    int panel_h = H - panel_y - 4;

    for (int i = 0; i < 4; i++) {
        lv_obj_t *p = lv_obj_create(s_scr_music);
        lv_obj_set_pos(p, 0, panel_y);
        lv_obj_set_size(p, W, panel_h);
        lv_obj_set_style_bg_color(p, C_BG, 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(p, 0, 0);
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        if (i != 0) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
        s_comms_panels[i] = p;
    }

    build_comms_spotify(s_comms_panels[0]);
    build_comms_weather(s_comms_panels[1]);
    build_comms_broadcast(s_comms_panels[2]);
    build_comms_lora(s_comms_panels[3]);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  ui_init / ui_splash_done
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_init(void) {
    ESP_LOGI(TAG, "Building BLUE-NET OS UI (%dx%d)", W, H);

    /* Build all screens (order matters: home refs other screens) */
    build_splash();
    /* Build sub-screens first so home can pass their pointers to tiles */
    build_eth();
    build_dash();
    build_cam();
    build_term();
    build_settings();
    build_net();
    build_gps();
    build_can();
    build_music();
    build_home();

    /* Shared keyboard on top layer */
    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, W, 350);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    /* Load splash screen */
    lv_screen_load(s_scr_splash);
    ESP_LOGI(TAG, "UI ready");
}

void ui_splash_done(void) {
    lv_screen_load_anim(s_scr_home, LV_SCR_LOAD_ANIM_FADE_IN, 600, 0, false);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Status bar updates
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_set_status(const char *msg) {
    if (s_home_status_lbl)  lv_label_set_text(s_home_status_lbl, msg);
    if (s_splash_status_lbl) lv_label_set_text(s_splash_status_lbl, msg);
}

void ui_set_wifi_ip(const char *ip) {
    if (s_home_ip_lbl) {
        lv_label_set_text(s_home_ip_lbl, ip);
        lv_obj_set_style_text_color(s_home_ip_lbl, C_GREEN, 0);
    }
    if (s_home_status_lbl) {
        lv_label_set_text(s_home_status_lbl, "WIFI ONLINE");
        lv_obj_set_style_text_color(s_home_status_lbl, C_GREEN, 0);
    }
}

void ui_set_time(const char *t) {
    for (int i = 0; i < s_hdr_lbl_cnt; i++)
        if (s_all_time_lbls[i]) lv_label_set_text(s_all_time_lbls[i], t);
}

void ui_set_time_date(const char *d) {
    for (int i = 0; i < s_hdr_lbl_cnt; i++)
        if (s_all_date_lbls[i]) lv_label_set_text(s_all_date_lbls[i], d);
}

void ui_set_battery(int pct, bool charging) {
    char buf[24];
    if (charging) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %d%%", pct);
    } else {
        const char *sym = (pct > 75) ? LV_SYMBOL_BATTERY_FULL :
                          (pct > 50) ? LV_SYMBOL_BATTERY_3 :
                          (pct > 25) ? LV_SYMBOL_BATTERY_2 :
                          (pct > 5)  ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        snprintf(buf, sizeof(buf), "%s %d%%", sym, pct);
    }
    lv_color_t c = (pct > 50) ? C_GREEN : (pct > 20) ? C_YELLOW : C_RED;
    for (int i = 0; i < s_hdr_lbl_cnt; i++) {
        if (s_all_batt_lbls[i]) {
            lv_label_set_text(s_all_batt_lbls[i], buf);
            lv_obj_set_style_text_color(s_all_batt_lbls[i], c, 0);
        }
    }
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  ETH updates
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_set_eth_status(const char *msg) {
    if (s_eth_status_lbl) lv_label_set_text(s_eth_status_lbl, msg);
}

void ui_set_eth_link(bool up) {
    if (!s_eth_link_dot) return;
    lv_obj_set_style_bg_color(s_eth_link_dot, up ? C_GREEN : C_FIRE, 0);
    if (!up) {
        if (s_eth_speed_lbl) {
            lv_label_set_text(s_eth_speed_lbl, "--");
            lv_obj_set_style_text_color(s_eth_speed_lbl, C_DIM, 0);
        }
        if (s_eth_duplex_lbl) {
            lv_label_set_text(s_eth_duplex_lbl, "--");
            lv_obj_set_style_text_color(s_eth_duplex_lbl, C_DIM, 0);
        }
    }
    if (s_eth_result_lbl && !s_flapping) {
        lv_label_set_text(s_eth_result_lbl, up ? "PASS" : "FAIL");
        lv_obj_set_style_text_color(s_eth_result_lbl,
                                    up ? C_GREEN : C_RED, 0);
    }
    if (s_eth_result_sub_lbl) {
        lv_obj_set_style_text_color(s_eth_result_sub_lbl,
                                    up ? C_GREEN : C_DIM, 0);
    }
}

void ui_set_eth_speed(int mbps, bool full_duplex) {
    if (s_eth_speed_lbl) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d Mbps", mbps);
        lv_label_set_text(s_eth_speed_lbl, buf);
        lv_obj_set_style_text_color(s_eth_speed_lbl, C_GREEN, 0);
    }
    if (s_eth_duplex_lbl) {
        lv_label_set_text(s_eth_duplex_lbl, full_duplex ? "FULL" : "HALF");
        lv_obj_set_style_text_color(s_eth_duplex_lbl,
                                    full_duplex ? C_GREEN : C_YELLOW, 0);
    }
}

void ui_set_eth_energy(bool detected) {
    if (s_eth_energy_lbl) {
        lv_label_set_text(s_eth_energy_lbl,
                          detected ? "PoE: DETECTED" : "PoE: NOT DETECTED");
        lv_obj_set_style_text_color(s_eth_energy_lbl,
                                    detected ? C_GREEN : C_DIM, 0);
    }
}

void ui_set_eth_dhcp(const char *ip_str) {
    if (s_eth_dhcp_lbl) {
        lv_label_set_text(s_eth_dhcp_lbl, ip_str);
        bool got = strncmp(ip_str, "waiting", 7) != 0 && strncmp(ip_str, "---", 3) != 0;
        lv_obj_set_style_text_color(s_eth_dhcp_lbl, got ? C_GREEN : C_DIM, 0);
    }
}

void ui_set_eth_net_info(const char *mac, const char *subnet,
                         const char *gw, const char *ports, const char *vlan) {
    if (s_eth_mac_lbl)    { lv_label_set_text(s_eth_mac_lbl, mac);
                            lv_obj_set_style_text_color(s_eth_mac_lbl, C_WHITE, 0); }
    if (s_eth_subnet_lbl) { lv_label_set_text(s_eth_subnet_lbl, subnet);
                            lv_obj_set_style_text_color(s_eth_subnet_lbl, C_WHITE, 0); }
    if (s_eth_gw_lbl)     { lv_label_set_text(s_eth_gw_lbl, gw);
                            lv_obj_set_style_text_color(s_eth_gw_lbl, C_WHITE, 0); }
    if (s_eth_ports_lbl)  { lv_label_set_text(s_eth_ports_lbl, ports);
                            lv_obj_set_style_text_color(s_eth_ports_lbl, C_WHITE, 0); }
    if (s_eth_vlan_lbl)   { lv_label_set_text(s_eth_vlan_lbl, vlan);
                            lv_obj_set_style_text_color(s_eth_vlan_lbl,
                                strncmp(vlan, "none", 4) == 0 ? C_DIM : C_YELLOW, 0); }
}

void ui_eth_log_append(const char *line) {
    if (!s_eth_conn_log) return;
    char buf[512];
    const char *cur = lv_label_get_text(s_eth_conn_log);
    if (cur && cur[0]) {
        snprintf(buf, sizeof(buf), "%s\n%s", cur, line);
    } else {
        snprintf(buf, sizeof(buf), "%s", line);
    }
    lv_label_set_text(s_eth_conn_log, buf);
    if (s_eth_conn_log_cont)
        lv_obj_scroll_to_y(s_eth_conn_log_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* state: 0=idle(dim), 1=busy(yellow), 2=ok(green), 3=fail(red) */
void ui_eth_set_shot_status(const char *msg, bool ok) {
    if (!s_eth_shot_lbl) return;
    lv_label_set_text(s_eth_shot_lbl, msg);
    /* SAVING... is passed with ok=true but has "..." — treat as yellow */
    lv_color_t c;
    if (!*msg)       c = C_DIM;
    else if (!ok)    c = C_RED;
    else if (msg[strlen(msg)-1] == '.') c = C_YELLOW;  /* ends in ... */
    else             c = C_GREEN;
    lv_obj_set_style_text_color(s_eth_shot_lbl, c, 0);
}

void ui_cable_set_pair(int pair, const char *status, int len_m, bool ok) {
    if (pair < 0 || pair > 3) return;
    lv_color_t col = ok ? C_GREEN : (strcmp(status, "TESTING") == 0 ? C_ORANGE : C_FIRE);
    if (s_cdt_dot[pair])
        lv_obj_set_style_bg_color(s_cdt_dot[pair], col, 0);
    if (s_cdt_pair_lbl[pair])
        lv_label_set_text(s_cdt_pair_lbl[pair], status);
    if (s_cdt_len_lbl[pair]) {
        if (len_m > 0) {
            char buf[16]; snprintf(buf, sizeof(buf), "~%dm", len_m);
            lv_label_set_text(s_cdt_len_lbl[pair], buf);
        } else {
            lv_label_set_text(s_cdt_len_lbl[pair], "");
        }
    }
}

void ui_cable_set_summary(bool pass, const char *mdi_str) {
    if (s_cdt_result_lbl) {
        lv_label_set_text(s_cdt_result_lbl, pass ? "PASS" : "FAIL");
        lv_obj_set_style_text_color(s_cdt_result_lbl, pass ? C_GREEN : C_FIRE, 0);
    }
    if (s_cdt_mdi_lbl)
        lv_label_set_text(s_cdt_mdi_lbl, mdi_str);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Terminal
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_term_append(const char *line) {
    if (s_term_log_lbl)
        log_append(s_term_log_lbl, s_term_log_cont, line,
                   s_term_buf, sizeof(s_term_buf));
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Network
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_net_set_status(const char *msg) {
    if (s_net_status_lbl) lv_label_set_text(s_net_status_lbl, msg);
}

void ui_net_clear_scan(void) {
    if (s_net_scan_cont) lv_obj_clean(s_net_scan_cont);
}

void ui_net_add_scan_result(const char *ssid, int rssi) {
    if (!s_net_scan_cont) return;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  (%d dBm)", ssid, rssi);
    lv_obj_t *row = lv_label_create(s_net_scan_cont);
    lv_label_set_text(row, buf);
    lv_obj_set_style_text_color(row, rssi > -70 ? C_GREEN : C_YELLOW, 0);
    lv_obj_set_style_text_font(row, &lv_font_montserrat_14, 0);
    lv_obj_set_width(row, W - 32);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  GPS
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_gps_update(double lat, double lon, float alt, float speed_kmh,
                   int sats, bool fix) {
    if (s_gps_fix_lbl) {
        lv_label_set_text(s_gps_fix_lbl, fix ? "FIX" : "NO FIX");
        lv_obj_set_style_text_color(s_gps_fix_lbl,
                                    fix ? C_GREEN : C_RED, 0);
    }
    if (s_gps_lat_lbl) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.6f", lat);
        lv_label_set_text(s_gps_lat_lbl, buf);
        lv_obj_set_style_text_color(s_gps_lat_lbl, fix ? C_WHITE : C_DIM, 0);
    }
    if (s_gps_lon_lbl) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.6f", lon);
        lv_label_set_text(s_gps_lon_lbl, buf);
        lv_obj_set_style_text_color(s_gps_lon_lbl, fix ? C_WHITE : C_DIM, 0);
    }
    if (s_gps_alt_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fm", alt);
        lv_label_set_text(s_gps_alt_lbl, buf);
    }
    if (s_gps_spd_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fkm/h", speed_kmh);
        lv_label_set_text(s_gps_spd_lbl, buf);
    }
    if (s_gps_sat_lbl) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", sats);
        lv_label_set_text(s_gps_sat_lbl, buf);
        lv_obj_set_style_text_color(s_gps_sat_lbl,
                                    sats >= 4 ? C_GREEN : C_YELLOW, 0);
    }
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  CAN bus
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_can_set_state(const char *state, bool active) {
    if (!s_can_state_lbl) return;
    lv_label_set_text(s_can_state_lbl, state);
    lv_obj_set_style_text_color(s_can_state_lbl, active ? C_GREEN : C_DIM, 0);
}

void ui_can_append_msg(const char *msg) {
    if (s_can_log_lbl)
        log_append(s_can_log_lbl, s_can_log_cont, msg,
                   s_can_buf, sizeof(s_can_buf));
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Camera
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_cam_set_frame(const void *rgb565, int w, int h) {
    (void)rgb565; (void)w; (void)h;
    s_cam_dirty = true;
}

void ui_cam_invalidate_cache(void) {
    s_cam_dirty = false;
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Music / Spotify
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_music_set_connected(bool connected) {
    if (!s_music_conn_lbl) return;
    lv_label_set_text(s_music_conn_lbl,
                      connected ? LV_SYMBOL_AUDIO "  SPOTIFY CONNECTED"
                                : "NOT CONNECTED");
    lv_obj_set_style_text_color(s_music_conn_lbl,
                                connected ? C_GREEN : C_DIM, 0);
}

void ui_music_set_track(const char *title, const char *artist, const char *album) {
    if (s_music_title_lbl)  lv_label_set_text(s_music_title_lbl,  title  ? title  : "---");
    if (s_music_artist_lbl) lv_label_set_text(s_music_artist_lbl, artist ? artist : "---");
    if (s_music_album_lbl)  lv_label_set_text(s_music_album_lbl,  album  ? album  : "");
}

void ui_music_set_playing(bool playing) {
    (void)playing;  /* play/pause icon could update here */
}

void ui_music_set_progress(int pos_ms, int dur_ms) {
    if (!s_music_prog_bar || !s_music_prog_lbl) return;
    int val = (dur_ms > 0) ? (int)((long long)pos_ms * 1000 / dur_ms) : 0;
    lv_bar_set_value(s_music_prog_bar, val, LV_ANIM_OFF);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d",
             pos_ms/60000, (pos_ms/1000)%60,
             dur_ms/60000, (dur_ms/1000)%60);
    lv_label_set_text(s_music_prog_lbl, buf);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Dashboard
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_dash_set_stats(int heap_kb, int psram_kb, float cpu0, float cpu1) {
    if (s_dash_heap_lbl) {
        char b[16]; snprintf(b, sizeof(b), "%d KB", heap_kb);
        lv_label_set_text(s_dash_heap_lbl, b);
    }
    if (s_dash_psram_lbl) {
        char b[16]; snprintf(b, sizeof(b), "%d KB", psram_kb);
        lv_label_set_text(s_dash_psram_lbl, b);
    }
    if (s_dash_cpu0_lbl) {
        char b[12]; snprintf(b, sizeof(b), "%.0f%%", cpu0);
        lv_label_set_text(s_dash_cpu0_lbl, b);
    }
    if (s_dash_cpu1_lbl) {
        char b[12]; snprintf(b, sizeof(b), "%.0f%%", cpu1);
        lv_label_set_text(s_dash_cpu1_lbl, b);
    }
}

void ui_dash_append_log(const char *line) {
    if (s_dash_log_lbl)
        log_append(s_dash_log_lbl, s_dash_log_cont, line,
                   s_dash_log_buf, sizeof(s_dash_log_buf));
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Settings
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_settings_set_ssid(const char *ssid) {
    if (s_settings_ssid_lbl)
        lv_label_set_text(s_settings_ssid_lbl,
                          (ssid && ssid[0]) ? ssid : "(none)");
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  Weather radio
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_weather_radio_append(const char *line) {
    if (s_wx_log_lbl)
        log_append(s_wx_log_lbl, s_wx_log_cont, line,
                   s_wx_buf, sizeof(s_wx_buf));
}

void ui_weather_radio_set_status(const char *msg, bool active) {
    if (!s_wx_status_lbl) return;
    lv_label_set_text(s_wx_status_lbl, msg);
    lv_obj_set_style_text_color(s_wx_status_lbl, active ? C_GREEN : C_DIM, 0);
}

/* ═════════════════════════════════════════════════════════════════════════════
 *  LoRa
 * ═══════════════════════════════════════════════════════════════════════════ */
void ui_lora_set_status(const char *msg, bool ok) {
    if (!s_lora_status_lbl) return;
    lv_label_set_text(s_lora_status_lbl, msg);
    lv_obj_set_style_text_color(s_lora_status_lbl, ok ? C_GREEN : C_RED, 0);
}

void ui_lora_append_rx(const char *msg, int rssi, int snr) {
    if (!s_lora_log_lbl) return;
    char buf[160];
    snprintf(buf, sizeof(buf), "RX[%ddBm SNR%d]: %s", rssi, snr, msg);
    log_append(s_lora_log_lbl, s_lora_log_cont, buf,
               s_lora_buf, sizeof(s_lora_buf));
}

void ui_lora_set_config(uint32_t freq_hz, int sf, int bw_khz, int power_dbm) {
    if (!s_lora_config_lbl) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%luMHz  SF%d  BW%d  +%ddBm",
             (unsigned long)(freq_hz / 1000000), sf, bw_khz, power_dbm);
    lv_label_set_text(s_lora_config_lbl, buf);
}
