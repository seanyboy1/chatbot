#include "obd.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "OBD";
static int s_sock = -1;

// ── Low-level TCP send/receive ────────────────────────────────────────────────

static int tcp_write(const char *data) {
    if (s_sock < 0) return -1;
    return send(s_sock, data, strlen(data), 0);
}

// Read until '>' prompt or timeout
static int tcp_read_until_prompt(char *buf, size_t len, int timeout_ms) {
    if (s_sock < 0) return -1;
    memset(buf, 0, len);
    int total = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (esp_timer_get_time() < deadline && total < (int)len - 1) {
        char c;
        struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s_sock, &rfds);
        int r = select(s_sock + 1, &rfds, NULL, NULL, &tv);
        if (r <= 0) continue;
        if (recv(s_sock, &c, 1, 0) <= 0) break;
        if (c == '\r' || c == '\n' || c == ' ') {
            // collapse whitespace
            if (total > 0 && buf[total-1] != ' ') buf[total++] = ' ';
            continue;
        }
        buf[total++] = c;
        if (c == '>') break;
    }
    // Trim trailing prompt and spaces
    while (total > 0 && (buf[total-1] == '>' || buf[total-1] == ' '))
        buf[--total] = '\0';
    return total;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool obd_connect(const char *host, uint16_t port) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "DNS failed for %s", host);
        return false;
    }

    s_sock = socket(res->ai_family, res->ai_socktype, 0);
    if (s_sock < 0) { freeaddrinfo(res); return false; }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(s_sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "TCP connect failed to %s:%u", host, port);
        close(s_sock);
        s_sock = -1;
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);
    ESP_LOGI(TAG, "TCP connected to %s:%u", host, port);
    return true;
}

bool obd_send(const char *cmd, char *buf, size_t buf_len) {
    char full[64];
    snprintf(full, sizeof(full), "%s\r", cmd);
    tcp_write(full);
    int n = tcp_read_until_prompt(buf, buf_len, OBD_TIMEOUT_MS);
    ESP_LOGD(TAG, "CMD %s -> [%s]", cmd, buf);
    return n >= 0;
}

bool obd_init(const char *protocol) {
    char buf[OBD_MAX_RESP];
    ESP_LOGI(TAG, "Initialising ELM327...");

    obd_send("ATZ",  buf, sizeof(buf)); vTaskDelay(pdMS_TO_TICKS(1000));
    obd_send("ATE0", buf, sizeof(buf)); // echo off
    obd_send("ATL0", buf, sizeof(buf)); // linefeeds off
    obd_send("ATS0", buf, sizeof(buf)); // spaces off
    obd_send("ATH0", buf, sizeof(buf)); // headers off

    char proto_cmd[8] = "ATSP0";
    if (protocol && protocol[0]) snprintf(proto_cmd, sizeof(proto_cmd), "ATSP%s", protocol);
    obd_send(proto_cmd, buf, sizeof(buf));

    ESP_LOGI(TAG, "ELM327 init done (proto=%s)", proto_cmd);
    return true;
}

// ── PID parsers ───────────────────────────────────────────────────────────────

// Parse hex pairs from ELM327 response after mode+PID echo
// e.g. "410C1AF8" -> bytes [0x1A, 0xF8], skipping first 2 bytes (41 0C)
static int parse_bytes(const char *resp, uint8_t *out, int max_bytes) {
    int len = strlen(resp);
    int count = 0;
    for (int i = 0; i < len - 1 && count < max_bytes; i += 2) {
        char h[3] = { resp[i], resp[i+1], '\0' };
        out[count++] = (uint8_t)strtol(h, NULL, 16);
    }
    // Skip mode (41) + PID bytes (first 2 bytes)
    if (count < 3) return 0;
    memmove(out, out + 2, count - 2);
    return count - 2;
}

static bool pid_query(const char *pid, uint8_t *out, int max_bytes, int *got) {
    char buf[OBD_MAX_RESP];
    if (!obd_send(pid, buf, sizeof(buf))) return false;
    if (strstr(buf, "NO DATA") || strstr(buf, "ERROR") || strstr(buf, "UNABLE")) return false;
    // Remove spaces from response
    char clean[OBD_MAX_RESP];
    int ci = 0;
    for (int i = 0; buf[i] && ci < (int)sizeof(clean)-1; i++)
        if (buf[i] != ' ') clean[ci++] = buf[i];
    clean[ci] = '\0';
    *got = parse_bytes(clean, out, max_bytes);
    return *got > 0;
}

bool obd_poll(obd_data_t *d) {
    uint8_t b[8];
    int got;

    // 010C — RPM = (A*256 + B) / 4
    if (pid_query("010C", b, sizeof(b), &got) && got >= 2)
        d->rpm = ((b[0] << 8) | b[1]) / 4.0f;

    // 010D — Speed km/h -> MPH
    if (pid_query("010D", b, sizeof(b), &got) && got >= 1)
        d->speed_mph = b[0] * 0.621371f;

    // 0105 — Coolant °C -> °F
    if (pid_query("0105", b, sizeof(b), &got) && got >= 1)
        d->coolant_f = (b[0] - 40) * 9.0f / 5.0f + 32.0f;

    // 010F — IAT °C -> °F
    if (pid_query("010F", b, sizeof(b), &got) && got >= 1)
        d->iat_f = (b[0] - 40) * 9.0f / 5.0f + 32.0f;

    // 0111 — Throttle %
    if (pid_query("0111", b, sizeof(b), &got) && got >= 1)
        d->throttle_pct = b[0] * 100.0f / 255.0f;

    // 012F — Fuel %
    if (pid_query("012F", b, sizeof(b), &got) && got >= 1)
        d->fuel_pct = b[0] * 100.0f / 255.0f;

    // 0142 — Battery V = (A*256 + B) / 1000
    if (pid_query("0142", b, sizeof(b), &got) && got >= 2)
        d->battery_v = ((b[0] << 8) | b[1]) / 1000.0f;

    // 010E — Timing advance = A/2 - 64
    if (pid_query("010E", b, sizeof(b), &got) && got >= 1)
        d->timing_deg = b[0] / 2.0f - 64.0f;

    // 0104 — Engine load %
    if (pid_query("0104", b, sizeof(b), &got) && got >= 1)
        d->engine_load_pct = b[0] * 100.0f / 255.0f;

    d->connected = true;
    return true;
}

void obd_disconnect(void) {
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
}
