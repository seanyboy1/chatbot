/*
 * ui.c — LVGL 9 cable-tester display for T-Display P4 (800×480)
 *
 * Layout:
 *  ┌──────────────────────────────────────────────────────────────────────────┐
 *  │  ⌁ BLUE-NET  ETH CABLE TESTER            192.168.1.50  ●  WIFI ONLINE  │  48px header
 *  ├──────────────────────────────────────────────────────────────────────────┤
 *  │                                                                          │
 *  │                    ┌────────────────────────┐                           │
 *  │                    │     CABLE RESULT       │                           │
 *  │                    │                        │                           │
 *  │                    │        PASS            │  (48pt, green or red)    │
 *  │                    │                        │                           │
 *  │                    └────────────────────────┘                           │
 *  │                                                                          │
 *  │   ┌──────────────┐      ┌──────────────┐      ┌──────────────┐         │
 *  │   │    SPEED     │      │    DUPLEX    │      │     LINK     │         │
 *  │   │   100 Mbps   │      │ FULL DUPLEX  │      │      ●       │         │
 *  │   └──────────────┘      └──────────────┘      └──────────────┘         │
 *  │                                                                          │
 *  │              PLUG ETHERNET CABLE INTO ETH PORT TO TEST                  │  footer
 *  └──────────────────────────────────────────────────────────────────────────┘
 */

#include "ui.h"
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "UI";

// ── Display dimensions ────────────────────────────────────────────────────────
#define DISP_W  800
#define DISP_H  480

// ── Colors ────────────────────────────────────────────────────────────────────
#define CLR_BG      lv_color_hex(0x0a0a0a)
#define CLR_PANEL   lv_color_hex(0x111111)
#define CLR_CARD    lv_color_hex(0x0d0d0d)
#define CLR_BORDER  lv_color_hex(0x1e1e1e)
#define CLR_ORANGE  lv_color_hex(0xFF4500)
#define CLR_GREEN   lv_color_hex(0x27c93f)
#define CLR_RED     lv_color_hex(0xFF3030)
#define CLR_YELLOW  lv_color_hex(0xFFBD2E)
#define CLR_DIM     lv_color_hex(0x444444)
#define CLR_DIMMER  lv_color_hex(0x2a2a2a)
#define CLR_WHITE   lv_color_hex(0xDDDDDD)
#define CLR_GRAY    lv_color_hex(0x666666)

// ── Widget handles ─────────────────────────────────────────────────────────────
static lv_obj_t *s_result_label;   // PASS / FAIL / -- --
static lv_obj_t *s_result_sub;     // "CABLE TEST RESULT" label above big text
static lv_obj_t *s_speed_val;      // "100 Mbps" / "--"
static lv_obj_t *s_duplex_val;     // "FULL DUPLEX" / "--"
static lv_obj_t *s_link_dot;       // colored dot in link card
static lv_obj_t *s_status_label;   // header right: status string
static lv_obj_t *s_ip_label;       // header right: wifi IP
static lv_obj_t *s_wifi_dot;       // small dot next to IP
static lv_obj_t *s_footer;         // bottom hint line
static lv_obj_t *s_fault_label;    // fault hint text below info row

// ── Style helpers ─────────────────────────────────────────────────────────────
static void set_text(lv_obj_t *obj, const lv_font_t *font, lv_color_t color, lv_text_align_t align) {
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_obj_set_style_text_align(obj, align, 0);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, CLR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

// ── ui_init ───────────────────────────────────────────────────────────────────
void ui_init(void) {
    ESP_LOGI(TAG, "Building cable-tester UI (%dx%d)", DISP_W, DISP_H);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header bar ────────────────────────────────────────────────────────────
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, DISP_W, 48);
    lv_obj_set_style_bg_color(header, CLR_PANEL, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, CLR_BORDER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  BLUE-NET  |  ETH CABLE TESTER  |  T-DISPLAY P4");
    set_text(title, &lv_font_montserrat_14, CLR_ORANGE, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 16, 0);

    // WiFi dot
    s_wifi_dot = lv_obj_create(header);
    lv_obj_set_size(s_wifi_dot, 10, 10);
    lv_obj_align(s_wifi_dot, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, CLR_DIM, 0);
    lv_obj_set_style_bg_opa(s_wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_wifi_dot, 0, 0);

    // Status / IP label
    s_ip_label = lv_label_create(header);
    lv_label_set_text(s_ip_label, "---.---.---.---");
    set_text(s_ip_label, &lv_font_montserrat_14, CLR_DIM, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(s_ip_label, LV_ALIGN_RIGHT_MID, -36, -8);

    s_status_label = lv_label_create(header);
    lv_label_set_text(s_status_label, "CONNECTING...");
    set_text(s_status_label, &lv_font_montserrat_12, CLR_DIM, LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(s_status_label, LV_ALIGN_RIGHT_MID, -36, 8);

    // ── Main result panel ─────────────────────────────────────────────────────
    // Centered vertically between header (48) and info row (bottom 140px)
    // Middle zone: y=48 to y=480-140=340, height=292, center=48+146=194
    lv_obj_t *result_panel = make_card(scr, 200, 60, 400, 190);
    lv_obj_set_style_border_color(result_panel, CLR_DIMMER, 0);

    s_result_sub = lv_label_create(result_panel);
    lv_label_set_text(s_result_sub, "CABLE TEST RESULT");
    set_text(s_result_sub, &lv_font_montserrat_12, CLR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_result_sub, LV_ALIGN_TOP_MID, 0, 18);

    s_result_label = lv_label_create(result_panel);
    lv_label_set_text(s_result_label, "-- --");
    set_text(s_result_label, &lv_font_montserrat_48, CLR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_result_label, LV_ALIGN_CENTER, 0, 10);

    // ── Three info cards ──────────────────────────────────────────────────────
    // Row at y=270, height=100. Three cards, evenly spaced across 800px.
    int card_y = 270;
    int card_h = 100;
    int card_w = 210;

    // Speed card
    lv_obj_t *speed_card = make_card(scr, 30, card_y, card_w, card_h);
    lv_obj_t *speed_lbl = lv_label_create(speed_card);
    lv_label_set_text(speed_lbl, "SPEED");
    set_text(speed_lbl, &lv_font_montserrat_12, CLR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(speed_lbl, LV_ALIGN_TOP_MID, 0, 14);
    s_speed_val = lv_label_create(speed_card);
    lv_label_set_text(s_speed_val, "--");
    set_text(s_speed_val, &lv_font_montserrat_24, CLR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_speed_val, LV_ALIGN_CENTER, 0, 8);

    // Duplex card
    lv_obj_t *duplex_card = make_card(scr, 295, card_y, card_w, card_h);
    lv_obj_t *duplex_lbl = lv_label_create(duplex_card);
    lv_label_set_text(duplex_lbl, "DUPLEX");
    set_text(duplex_lbl, &lv_font_montserrat_12, CLR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(duplex_lbl, LV_ALIGN_TOP_MID, 0, 14);
    s_duplex_val = lv_label_create(duplex_card);
    lv_label_set_text(s_duplex_val, "--");
    set_text(s_duplex_val, &lv_font_montserrat_20, CLR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_duplex_val, LV_ALIGN_CENTER, 0, 8);

    // Link card
    lv_obj_t *link_card = make_card(scr, 560, card_y, card_w, card_h);
    lv_obj_t *link_lbl = lv_label_create(link_card);
    lv_label_set_text(link_lbl, "LINK");
    set_text(link_lbl, &lv_font_montserrat_12, CLR_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(link_lbl, LV_ALIGN_TOP_MID, 0, 14);
    s_link_dot = lv_obj_create(link_card);
    lv_obj_set_size(s_link_dot, 28, 28);
    lv_obj_align(s_link_dot, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_link_dot, CLR_DIM, 0);
    lv_obj_set_style_bg_opa(s_link_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_link_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_link_dot, 0, 0);

    // ── Fault hint ─────────────────────────────────────────────────────────────
    s_fault_label = lv_label_create(scr);
    lv_label_set_text(s_fault_label, "");
    set_text(s_fault_label, &lv_font_montserrat_12, CLR_ORANGE, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_fault_label, DISP_W - 60);
    lv_obj_align(s_fault_label, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_long_mode(s_fault_label, LV_LABEL_LONG_WRAP);

    // ── Footer hint ────────────────────────────────────────────────────────────
    s_footer = lv_label_create(scr);
    lv_label_set_text(s_footer, "PLUG ETHERNET CABLE INTO ETH PORT TO TEST");
    set_text(s_footer, &lv_font_montserrat_12, CLR_DIMMER, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -14);

    ESP_LOGI(TAG, "UI ready");
}

// ── ui_set_eth_result ─────────────────────────────────────────────────────────
void ui_set_eth_result(bool linked, int speed_mbps, bool full_duplex) {
    if (linked) {
        // Big PASS in green
        lv_label_set_text(s_result_label, "PASS");
        lv_obj_set_style_text_color(s_result_label, CLR_GREEN, 0);
        lv_obj_set_style_text_color(s_result_sub, CLR_GREEN, 0);

        // Link dot green
        lv_obj_set_style_bg_color(s_link_dot, CLR_GREEN, 0);

        // Speed
        char spd[24];
        snprintf(spd, sizeof(spd), "%d Mbps", speed_mbps);
        lv_label_set_text(s_speed_val, spd);
        lv_obj_set_style_text_color(s_speed_val, CLR_GREEN, 0);

        // Duplex
        bool is_full = full_duplex;
        lv_label_set_text(s_duplex_val, is_full ? "FULL" : "HALF");
        lv_obj_set_style_text_color(s_duplex_val, is_full ? CLR_GREEN : CLR_YELLOW, 0);

        // Hide fault hint, show footer
        lv_label_set_text(s_fault_label, "");
        lv_label_set_text(s_footer, "");
    } else {
        // Big FAIL in red
        lv_label_set_text(s_result_label, "FAIL");
        lv_obj_set_style_text_color(s_result_label, CLR_RED, 0);
        lv_obj_set_style_text_color(s_result_sub, CLR_DIM, 0);

        // Link dot red
        lv_obj_set_style_bg_color(s_link_dot, CLR_RED, 0);

        // Clear speed/duplex
        lv_label_set_text(s_speed_val, "--");
        lv_obj_set_style_text_color(s_speed_val, CLR_DIM, 0);
        lv_label_set_text(s_duplex_val, "--");
        lv_obj_set_style_text_color(s_duplex_val, CLR_DIM, 0);

        // Fault hint
        lv_label_set_text(s_fault_label,
            "NO LINK  |  Check both connectors are seated  |  "
            "Verify far-end device is powered  |  Try a known-good cable");
        lv_label_set_text(s_footer, "");
    }
}

// ── ui_set_status ─────────────────────────────────────────────────────────────
void ui_set_status(const char *msg) {
    lv_label_set_text(s_status_label, msg);
}

// ── ui_set_wifi_ip ────────────────────────────────────────────────────────────
void ui_set_wifi_ip(const char *ip) {
    lv_label_set_text(s_ip_label, ip);
    lv_obj_set_style_text_color(s_ip_label, CLR_GREEN, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, CLR_GREEN, 0);
    lv_label_set_text(s_status_label, "WIFI ONLINE");
    lv_obj_set_style_text_color(s_status_label, CLR_GREEN, 0);
}
