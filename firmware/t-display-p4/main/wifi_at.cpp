/*
 * wifi_at.cpp — WiFi via ESP32-C6 AT firmware over SDIO (slot 1)
 *
 * Uses cpp_bus_driver's EspAt class to drive the C6's factory AT firmware
 * over the same SDIO bus that would be used by esp_hosted.  No firmware
 * flash required — C6 ships with AT firmware v4.0.0.0.
 *
 * SDIO slot 1: CLK=GPIO18, CMD=GPIO19, D0-D3=GPIO14-17
 */

#include "wifi_at.h"

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "cpp_bus_driver_library.h"

#define TAG "WIFI_AT"

// ── SDIO pins — C6 SDIO slave, slot 1 ────────────────────────────────────────
#define C6_SDIO_CLK  18
#define C6_SDIO_CMD  19
#define C6_SDIO_D0   14
#define C6_SDIO_D1   15
#define C6_SDIO_D2   16
#define C6_SDIO_D3   17

// ── Module-level state ────────────────────────────────────────────────────────
static c6_en_fn_t s_en_fn = nullptr;
static std::shared_ptr<cpp_bus_driver::HardwareSdio> s_sdio;
static std::unique_ptr<cpp_bus_driver::EspAt>        s_at;
static char s_ip[40] = "";

// ── Reset trampoline (static — safe to use as C function pointer) ─────────────
static void c6_reset_trampoline(bool high) {
    if (s_en_fn) s_en_fn(high);
}

// Forward declarations
static bool at_ensure_link(void);
static bool parse_url(const char *url, std::string &scheme, std::string &host,
                      int &port, std::string &path);

// ── Raw AT command helper ─────────────────────────────────────────────────────
// Sends cmd, polls for response packets until OK/ERROR/timeout, returns full resp.
static std::string send_at_raw(const std::string &cmd, int timeout_ms = 5000) {
    s_at->SendPacket(cmd);
    std::string result;
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        std::vector<uint8_t> buf;
        if (s_at->ReceivePacket(buf) && !buf.empty()) {
            buf.push_back('\0');
            result += reinterpret_cast<const char *>(buf.data());
            if (result.find("OK\r\n")    != std::string::npos ||
                result.find("ERROR\r\n") != std::string::npos ||
                result.find("FAIL\r\n")  != std::string::npos) {
                break;
            }
            continue;  // chain reads while data is flowing
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        elapsed += 20;
    }
    return result;
}

// ── Public C API ─────────────────────────────────────────────────────────────

extern "C" bool wifi_at_init(c6_en_fn_t en_fn, const char *ssid, const char *pass) {
    s_en_fn = en_fn;

    s_sdio = std::make_shared<cpp_bus_driver::HardwareSdio>(
        C6_SDIO_CLK, C6_SDIO_CMD,
        C6_SDIO_D0, C6_SDIO_D1, C6_SDIO_D2, C6_SDIO_D3,
        CPP_BUS_DRIVER_DEFAULT_VALUE, CPP_BUS_DRIVER_DEFAULT_VALUE,
        CPP_BUS_DRIVER_DEFAULT_VALUE, CPP_BUS_DRIVER_DEFAULT_VALUE,
        cpp_bus_driver::HardwareSdio::SdioPort::kSlot1
    );

    s_at = std::make_unique<cpp_bus_driver::EspAt>(s_sdio, c6_reset_trampoline);

    ESP_LOGI(TAG, "Initialising SDIO-AT link to C6...");
    if (!s_at->Init()) {
        ESP_LOGE(TAG, "EspAt::Init() failed — C6 SDIO slave not responding");
        return false;
    }
    ESP_LOGI(TAG, "SDIO-AT link established");

    s_at->SetFlashSave(true);

    if (!s_at->SetWifiMode(cpp_bus_driver::EspAt::WifiMode::kStation)) {
        ESP_LOGE(TAG, "SetWifiMode(station) failed");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to WiFi '%s'...", ssid);
    // SetWifiConnect blocks up to its timeout (default 5 s) for WIFI GOT IP
    if (!s_at->SetWifiConnect(std::string(ssid), std::string(pass), 15000)) {
        ESP_LOGE(TAG, "SetWifiConnect failed — wrong credentials or AP out of range");
        return false;
    }
    ESP_LOGI(TAG, "WiFi connected");

    // Retrieve assigned IP
    std::string cifsr = send_at_raw("AT+CIFSR\r\n", 3000);
    auto pos = cifsr.find("STAIP,\"");
    if (pos != std::string::npos) {
        pos += 7;
        auto end = cifsr.find('"', pos);
        if (end != std::string::npos) {
            std::string ip = cifsr.substr(pos, end - pos);
            strncpy(s_ip, ip.c_str(), sizeof(s_ip) - 1);
            s_ip[sizeof(s_ip) - 1] = '\0';
            ESP_LOGI(TAG, "WiFi IP: %s", s_ip);
        }
    }

    // Enable SNTP so GetRealTime() works
    send_at_raw("AT+CIPSNTPCFG=1,0,\"pool.ntp.org\",\"time.cloudflare.com\"\r\n", 3000);

    return true;
}

extern "C" bool wifi_at_is_connected(void) {
    return s_at && s_at->GetConnectStatus();
}

extern "C" void wifi_at_get_ip(char *buf, size_t len) {
    strncpy(buf, s_ip, len - 1);
    buf[len - 1] = '\0';
}

extern "C" time_t wifi_at_get_epoch(void) {
    if (!s_at) return 0;
    // SNTP may need a few seconds to sync after connect
    for (int i = 0; i < 8; i++) {
        cpp_bus_driver::EspAt::RealTime rt;
        if (s_at->GetRealTime(rt) && rt.year > 2020) {
            struct tm t = {};
            t.tm_year = rt.year - 1900;
            t.tm_mon  = rt.month - 1;
            t.tm_mday = rt.day;
            t.tm_hour = rt.hour;
            t.tm_min  = rt.minute;
            t.tm_sec  = rt.second;
            t.tm_isdst = -1;
            time_t epoch = mktime(&t);
            if (epoch > 1000000000LL) return epoch;
        }
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    return 0;
}

extern "C" int wifi_at_http_get(const char *url, char **out_buf) {
    if (!s_at || !out_buf) return -1;

    if (!at_ensure_link()) {
        ESP_LOGE(TAG, "HTTP GET: AT link dead");
        return -1;
    }

    // Parse URL into scheme/host/port/path
    std::string scheme, host, path;
    int port = 80;
    if (!parse_url(url, scheme, host, port, path)) {
        ESP_LOGE(TAG, "HTTP GET: bad URL %s", url);
        return -1;
    }

    // Close any stale connection first
    send_at_raw("AT+CIPCLOSE\r\n", 500);
    send_at_raw("AT+CIPMUX=0\r\n", 500);
    send_at_raw("AT+CIPRECVMODE=0\r\n", 500);

    // Open TCP or SSL connection
    char cipstart[128];
    snprintf(cipstart, sizeof(cipstart), "AT+CIPSTART=\"%s\",\"%s\",%d\r\n",
             scheme.c_str(), host.c_str(), port);
    std::string r = send_at_raw(cipstart, 10000);
    if (r.find("CONNECT") == std::string::npos && r.find("OK") == std::string::npos) {
        ESP_LOGE(TAG, "HTTP GET: CIPSTART failed: %.60s", r.c_str());
        return -1;
    }

    // Build HTTP/1.0 GET request (1.0 = no chunked encoding, server closes after response)
    std::string req = "GET " + path + " HTTP/1.0\r\n";
    req += "Host: " + host + "\r\n";
    req += "Connection: close\r\n\r\n";

    // Send with AT+CIPSEND
    char cipsend[32];
    snprintf(cipsend, sizeof(cipsend), "AT+CIPSEND=%d\r\n", (int)req.size());
    std::string sr = send_at_raw(cipsend, 3000);
    if (sr.find('>') == std::string::npos) {
        ESP_LOGE(TAG, "HTTP GET: no > prompt");
        send_at_raw("AT+CIPCLOSE\r\n", 1000);
        return -1;
    }
    s_at->SendPacket(req);

    // Collect all data until CLOSED or timeout
    std::vector<uint8_t> raw;
    int elapsed = 0;
    bool closed = false;
    while (elapsed < 20000 && !closed) {
        std::vector<uint8_t> pkt;
        if (s_at->ReceivePacket(pkt) && !pkt.empty()) {
            raw.insert(raw.end(), pkt.begin(), pkt.end());
            // Check for connection close marker
            std::string tail(raw.end() - std::min((int)raw.size(), 32), raw.end());
            if (tail.find("CLOSED") != std::string::npos ||
                tail.find("0,CLOSED") != std::string::npos) {
                closed = true;
            }
            elapsed = 0;  // reset idle timer on data
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        elapsed += 20;
    }

    send_at_raw("AT+CIPCLOSE\r\n", 500);

    if (raw.empty()) {
        ESP_LOGE(TAG, "HTTP GET: no data received");
        return -1;
    }

    // Strip +IPD framing if present
    std::vector<uint8_t> clean;
    {
        std::vector<uint8_t> tmp = raw;
        std::vector<uint8_t> dummy;
        // Reuse extract_ipd logic inline: scan for +IPD frames
        std::string s(tmp.begin(), tmp.end());
        if (s.find("+IPD,") != std::string::npos) {
            // +IPD mode: extract body bytes
            size_t p = 0;
            while ((p = s.find("+IPD,", p)) != std::string::npos) {
                auto colon = s.find(':', p + 5);
                if (colon == std::string::npos) break;
                int ilen = atoi(s.c_str() + p + 5);
                size_t ds = colon + 1;
                if (ds + (size_t)ilen <= s.size()) {
                    clean.insert(clean.end(), tmp.begin() + ds, tmp.begin() + ds + ilen);
                    p = ds + ilen;
                } else break;
            }
        } else {
            clean = tmp;
        }
    }

    // Skip HTTP response headers (\r\n\r\n separator)
    std::string resp(clean.begin(), clean.end());
    auto hdr_end = resp.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        ESP_LOGE(TAG, "HTTP GET: no header separator");
        return -1;
    }

    // Verify 200 OK
    if (resp.find("200 ") == std::string::npos && resp.find("200\r") == std::string::npos) {
        ESP_LOGE(TAG, "HTTP GET: non-200: %.60s", resp.c_str());
        return -1;
    }

    size_t body_start = hdr_end + 4;
    size_t body_len = resp.size() - body_start;
    if (body_len == 0) {
        ESP_LOGE(TAG, "HTTP GET: empty body");
        return -1;
    }

    ESP_LOGI(TAG, "HTTP GET OK: %d body bytes from %s", (int)body_len, host.c_str());

    *out_buf = static_cast<char *>(malloc(body_len + 1));
    if (!*out_buf) return -1;
    memcpy(*out_buf, resp.c_str() + body_start, body_len);
    (*out_buf)[body_len] = '\0';
    return (int)body_len;
}

// ── TCP/SSL streaming via AT+CIPSEND=N / +IPD mode (no transparent mode) ──────
// No AT+CIPMODE=1, no +++ escape.  Send HTTP GET with CIPSEND=N, receive
// data as "+IPD,<len>:<data>" unsolicited notifications.  Reliable across retries.

struct wifi_at_stream_s {
    bool connected;
    std::vector<uint8_t> raw;    // unparsed SDIO bytes (partial +IPD frames)
    std::vector<uint8_t> body;   // extracted HTTP body bytes ready for MP3 decoder
};

static struct wifi_at_stream_s s_stream_obj;
static char s_stream_error[64] = "";

// Parse scheme://host[:port]/path  →  scheme="SSL"|"TCP", host, port, path
static bool parse_url(const char *url, std::string &scheme, std::string &host,
                      int &port, std::string &path) {
    std::string u(url);
    if (u.substr(0, 8) == "https://") { scheme = "SSL"; port = 443; u = u.substr(8); }
    else if (u.substr(0, 7) == "http://") { scheme = "TCP"; port = 80; u = u.substr(7); }
    else return false;
    auto sl = u.find('/');
    std::string hp = (sl != std::string::npos) ? u.substr(0, sl) : u;
    path = (sl != std::string::npos) ? u.substr(sl) : "/";
    auto co = hp.find(':');
    if (co != std::string::npos) { host = hp.substr(0, co); port = atoi(hp.substr(co+1).c_str()); }
    else host = hp;
    return !host.empty();
}

// Poll for any pending SDIO-AT data, append to acc.  Returns bytes appended.
static int poll_rx(std::vector<uint8_t> &acc, int wait_ms = 200) {
    int total = 0;
    int elapsed = 0;
    while (elapsed < wait_ms) {
        uint32_t flag = s_at->GetIrqFlag();
        if (s_at->ParseRxNewPacketFlag(flag)) {
            s_at->ClearIrqFlag(flag);
            std::vector<uint8_t> pkt;
            s_at->ReceivePacket(pkt);
            acc.insert(acc.end(), pkt.begin(), pkt.end());
            total += pkt.size();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        elapsed += 20;
    }
    return total;
}

// Binary-safe passive-mode poll.
// Sends AT+CIPRECVDATA=N, waits for C6 response via SDIO IRQ, extracts exactly
// len bytes of payload binary-safe (no null-termination tricks).
// Returns number of bytes placed in out (0 = no data yet, -1 = error/closed).
static int do_ciprecvdata(int max_bytes, std::vector<uint8_t> &out, int timeout_ms = 2000) {
    std::string cmd = "AT+CIPRECVDATA=" + std::to_string(max_bytes) + "\r\n";
    s_at->SendPacket(cmd);

    std::vector<uint8_t> resp;
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        uint32_t flag = s_at->GetIrqFlag();
        if (s_at->ParseRxNewPacketFlag(flag)) {
            s_at->ClearIrqFlag(flag);
            std::vector<uint8_t> pkt;
            s_at->ReceivePacket(pkt);
            resp.insert(resp.end(), pkt.begin(), pkt.end());
        }

        // Scan for "+CIPRECVDATA,<len>:<data>"
        const char *tag = "+CIPRECVDATA,";
        auto it = std::search(resp.begin(), resp.end(),
                              (const uint8_t *)tag, (const uint8_t *)tag + 13);
        if (it != resp.end()) {
            size_t after_tag = (it - resp.begin()) + 13;
            // Find colon
            auto colon = std::find(resp.begin() + after_tag, resp.end(), (uint8_t)':');
            if (colon != resp.end()) {
                size_t colon_idx = colon - resp.begin();
                int dlen = atoi(std::string(resp.begin() + after_tag, colon).c_str());
                if (dlen <= 0) return 0;  // "+CIPRECVDATA,0" — no data
                size_t data_start = colon_idx + 1;
                if (data_start + (size_t)dlen <= resp.size()) {
                    out.insert(out.end(), resp.begin() + data_start,
                               resp.begin() + data_start + dlen);
                    return dlen;
                }
                // Partial data — keep collecting (don't re-send AT command)
            }
            // Found tag but no colon yet — wait for more bytes
        } else {
            // Check terminal conditions (no +CIPRECVDATA)
            std::string s(resp.begin(), resp.end());
            if (s.find("\r\nOK\r\n")    != std::string::npos) return 0;   // OK with no data
            if (s.find("ERROR\r\n")     != std::string::npos) return -1;
            if (s.find("0,CLOSED\r\n") != std::string::npos) return -1;
            if (s.find("CLOSED\r\n")   != std::string::npos) return -1;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
        elapsed += 20;
    }
    return 0;  // timeout — caller can retry
}

static stream_status_fn_t s_status_cb = nullptr;
static wifi_at_stream_t stream_open_url(const char *url, int redirect_depth);

// Verify AT module is alive; if connect_.status dropped, re-init the SDIO link.
static bool at_ensure_link(void) {
    if (!s_at->GetConnectStatus()) {
        ESP_LOGW(TAG, "SDIO connect_.status lost — re-init AT link");
        if (!s_at->Init()) {
            ESP_LOGE(TAG, "AT re-init failed");
            return false;
        }
        for (int i = 0; i < 150; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            std::string r = send_at_raw("AT+CIFSR\r\n", 2000);
            if (r.find("STAIP,\"") != std::string::npos &&
                r.find("0.0.0.0") == std::string::npos) {
                ESP_LOGI(TAG, "WiFi back after re-init");
                return true;
            }
        }
        ESP_LOGE(TAG, "WiFi did not reconnect after re-init");
        return false;
    }
    std::string r = send_at_raw("AT\r\n", 1000);
    if (r.find("OK") == std::string::npos) {
        ESP_LOGW(TAG, "AT sync failed: %s", r.c_str());
        return false;
    }
    return true;
}

// Extract all complete +IPD frames from raw into body.
// Returns false if CLOSED/ERROR detected (stream ended).
static bool extract_ipd(std::vector<uint8_t> &raw, std::vector<uint8_t> &body) {
    while (raw.size() >= 7) {   // "+IPD,1:" is 7 chars minimum
        const char *tag = "+IPD,";
        auto it = std::search(raw.begin(), raw.end(),
                              (const uint8_t *)tag, (const uint8_t *)tag + 5);
        if (it == raw.end()) {
            std::string s(raw.begin(), raw.end());
            if (s.find("CLOSED") != std::string::npos ||
                s.find("ERROR\r\n") != std::string::npos) return false;
            break;
        }
        // Discard junk before +IPD,
        if (it != raw.begin()) raw.erase(raw.begin(), it);

        auto colon = std::find(raw.begin() + 5, raw.end(), (uint8_t)':');
        if (colon == raw.end()) break;  // incomplete header — wait

        size_t colon_pos = colon - raw.begin();
        std::string len_str(raw.begin() + 5, colon);
        int ipd_len = atoi(len_str.c_str());
        if (ipd_len <= 0 || ipd_len > 16384) {
            raw.erase(raw.begin(), raw.begin() + colon_pos + 1);
            continue;
        }
        size_t data_start = colon_pos + 1;
        if (data_start + (size_t)ipd_len > raw.size()) break;  // data incomplete — wait

        body.insert(body.end(), raw.begin() + data_start,
                    raw.begin() + data_start + ipd_len);
        raw.erase(raw.begin(), raw.begin() + data_start + ipd_len);
    }
    return true;
}

static wifi_at_stream_t stream_open_url(const char *url, int redirect_depth) {
    if (redirect_depth > 3) {
        ESP_LOGE(TAG, "stream: too many redirects");
        return nullptr;
    }

    std::string scheme, host, path;
    int port;
    if (!parse_url(url, scheme, host, port, path)) {
        ESP_LOGE(TAG, "stream: bad URL: %s", url);
        return nullptr;
    }

    if (!at_ensure_link()) {
        snprintf(s_stream_error, sizeof(s_stream_error), "AT LINK DEAD");
        return nullptr;
    }

    // Active-push receive mode.
    std::string r;
    r = send_at_raw("AT+CIPMODE=0\r\n", 1000);
    ESP_LOGI(TAG, "CIPMODE resp: %.30s", r.c_str());
    r = send_at_raw("AT+CIPRECVMODE=0\r\n", 1000);
    ESP_LOGI(TAG, "CIPRECVMODE resp: %.30s", r.c_str());
    r = send_at_raw("AT+CIPMUX=0\r\n", 1000);
    ESP_LOGI(TAG, "CIPMUX resp: %.30s", r.c_str());
    send_at_raw("AT+CIPCLOSE\r\n", 1000);

    { std::vector<uint8_t> drain; poll_rx(drain, 500); }

    if (scheme == "SSL") send_at_raw("AT+CIPSSLCCONF=0,0\r\n", 1000);

    // CIPSTART — poll until "CONNECT"
    std::string cipstart = "AT+CIPSTART=\"" + scheme + "\",\"" + host +
                           "\"," + std::to_string(port) + "\r\n";
    ESP_LOGI(TAG, "stream: %s", cipstart.c_str());
    s_at->SendPacket(cipstart);

    std::string cr;
    for (int i = 0; i < 250; i++) {
        uint32_t flag = s_at->GetIrqFlag();
        if (s_at->ParseRxNewPacketFlag(flag)) {
            s_at->ClearIrqFlag(flag);
            std::vector<uint8_t> pkt;
            s_at->ReceivePacket(pkt);
            pkt.push_back('\0');
            cr += reinterpret_cast<const char *>(pkt.data());
            ESP_LOGI(TAG, "CIPSTART rx: %s", cr.c_str());
        }
        if (cr.find("CONNECT") != std::string::npos ||
            cr.find("ALREADY")  != std::string::npos) break;
        if (cr.find("ERROR\r\n") != std::string::npos ||
            cr.find("CLOSED\r\n")!= std::string::npos) break;
        if (s_status_cb && (i % 50 == 0)) {
            char msg[32];
            snprintf(msg, sizeof(msg), "CIPSTART %ds...", i / 50);
            s_status_cb(msg);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (cr.find("CONNECT") == std::string::npos &&
        cr.find("ALREADY")  == std::string::npos) {
        std::string safe;
        for (char c : cr) if (c >= 0x20 && c < 0x7f) safe += c;
        char hex[52] = "";
        if (safe.empty()) {
            for (size_t k = 0; k < cr.size() && k < 8; k++)
                snprintf(hex + k*3, sizeof(hex) - k*3, "%02X ", (uint8_t)cr[k]);
            snprintf(s_stream_error, sizeof(s_stream_error), "HEX:%.46s", hex);
        } else {
            snprintf(s_stream_error, sizeof(s_stream_error), "%.40s", safe.c_str());
        }
        ESP_LOGE(TAG, "stream: CIPSTART failed: [%s]", s_stream_error);
        send_at_raw("AT+CIPCLOSE\r\n", 2000);
        return nullptr;
    }
    s_stream_error[0] = '\0';

    // Send HTTP GET via AT+CIPSEND=N (no transparent mode)
    // Icy-MetaData: 0 — suppress ICY metadata injection (would corrupt MP3 bitstream)
    std::string req = "GET " + path + " HTTP/1.0\r\n"
                      "Host: " + host + "\r\n"
                      "User-Agent: WinampMPEG/5.0\r\n"
                      "Accept: */*\r\n"
                      "Icy-MetaData: 0\r\n\r\n";

    std::string cipsend = "AT+CIPSEND=" + std::to_string(req.size()) + "\r\n";
    s_at->SendPacket(cipsend);

    // Wait for '>' prompt
    std::string prompt_resp;
    for (int i = 0; i < 150; i++) {
        uint32_t flag = s_at->GetIrqFlag();
        if (s_at->ParseRxNewPacketFlag(flag)) {
            s_at->ClearIrqFlag(flag);
            std::vector<uint8_t> pkt;
            s_at->ReceivePacket(pkt);
            prompt_resp += std::string(pkt.begin(), pkt.end());
        }
        if (prompt_resp.find('>') != std::string::npos) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (prompt_resp.find('>') == std::string::npos) {
        ESP_LOGE(TAG, "stream: no > prompt, got: %.40s", prompt_resp.c_str());
        snprintf(s_stream_error, sizeof(s_stream_error), "NO > PROMPT");
        send_at_raw("AT+CIPCLOSE\r\n", 2000);
        return nullptr;
    }

    bool sp_ok = s_at->SendPacket(req);
    ESP_LOGI(TAG, "stream: SendPacket HTTP GET %zu bytes: %s", req.size(), sp_ok ? "OK" : "FAILED");
    if (!sp_ok) {
        snprintf(s_stream_error, sizeof(s_stream_error), "SEND PKT FAIL");
        send_at_raw("AT+CIPCLOSE\r\n", 2000);
        return nullptr;
    }

    // Wait for SEND OK — poll at 20ms to avoid SDIO buffer filling before first drain
    std::string send_resp;
    for (int i = 0; i < 250; i++) {
        uint32_t flag = s_at->GetIrqFlag();
        if (s_at->ParseRxNewPacketFlag(flag)) {
            s_at->ClearIrqFlag(flag);
            vTaskDelay(pdMS_TO_TICKS(5));  // settle: C6 DMA needs ~5ms after IRQ clear
        }
        // Always poll — ICY response can arrive bundled with or just after SEND OK
        {
            std::vector<uint8_t> pkt;
            s_at->ReceivePacket(pkt);
            if (!pkt.empty()) send_resp += std::string(pkt.begin(), pkt.end());
        }
        if (send_resp.find("SEND OK")   != std::string::npos ||
            send_resp.find("SEND FAIL") != std::string::npos ||
            send_resp.find("ERROR")     != std::string::npos) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (send_resp.find("SEND OK") == std::string::npos) {
        ESP_LOGE(TAG, "stream: CIPSEND failed: %.40s", send_resp.c_str());
        snprintf(s_stream_error, sizeof(s_stream_error), "SEND FAILED");
        send_at_raw("AT+CIPCLOSE\r\n", 2000);
        return nullptr;
    }

    // Active-mode header collection.
    // Seed acc from send_resp: the server's ICY response often arrives bundled
    // with the SEND OK preamble and is consumed by the SEND OK wait loop above.
    // Filter out SDIO null-padding bytes (0x00) — HTTP headers are pure text.
    std::vector<uint8_t> acc;
    for (size_t i = 0; i < send_resp.size(); i++)
        if ((uint8_t)send_resp[i]) acc.push_back((uint8_t)send_resp[i]);

    int irq_count = 0;
    for (int tries = 0; tries < 300; tries++) {   // up to 30 s
        // IRQ path — pure polling, no embedded AT commands that corrupt SDIO state
        uint32_t flag = s_at->GetIrqFlag();
        bool cs = s_at->GetConnectStatus();
        if (tries < 50 || tries % 100 == 0)
            ESP_LOGI(TAG, "hdr[%d] irq=%08X cs=%d acc=%zu irqN=%d", tries, flag, cs, acc.size(), irq_count);
        if (!cs) {
            ESP_LOGW(TAG, "hdr: connect_.status dropped false at try %d", tries);
            if (s_status_cb) s_status_cb("CS=FALSE");
            break;
        }
        if (s_at->ParseRxNewPacketFlag(flag)) {
            irq_count++;
            s_at->ClearIrqFlag(flag);
            // Give C6 SDIO slave ~5 ms to set up DMA after interrupt clear,
            // otherwise the ReadBlock inside ReceivePacket times out (0x102).
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        // Always poll for data — don't rely on bit-23 re-assertion after clear.
        {
            std::vector<uint8_t> pkt;
            if (s_at->ReceivePacket(pkt) && !pkt.empty()) {
                // Skip all-null SDIO padding packets
                bool has_data = false;
                for (auto b : pkt) if (b) { has_data = true; break; }
                if (has_data) {
                    ESP_LOGI(TAG, "hdr pkt=%zu [%02X %02X %02X %02X %02X %02X]",
                        pkt.size(),
                        pkt.size()>0?pkt[0]:0, pkt.size()>1?pkt[1]:0,
                        pkt.size()>2?pkt[2]:0, pkt.size()>3?pkt[3]:0,
                        pkt.size()>4?pkt[4]:0, pkt.size()>5?pkt[5]:0);
                    acc.insert(acc.end(), pkt.begin(), pkt.end());
                    // Detect TCP close
                    std::string tmp(pkt.begin(), pkt.end());
                    if (tmp.find("CLOSED\r\n") != std::string::npos ||
                        tmp.find("0,CLOSED") != std::string::npos) {
                        ESP_LOGW(TAG, "hdr: TCP CLOSED");
                        if (s_status_cb) s_status_cb("TCP CLOSED");
                        break;
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));

        if (s_status_cb && (tries % 50 == 0)) {
            char msg[40];
            snprintf(msg, sizeof(msg), "H%d a=%zu i=%d", tries / 50, acc.size(), irq_count);
            s_status_cb(msg);
        }

        if (acc.size() < 4) continue;

        std::string as(acc.begin(), acc.end());

        // Wait until we see the start of an HTTP or ICY response
        auto h1  = as.find("HTTP/");
        auto icy = as.find("ICY ");
        if (h1 == std::string::npos && icy == std::string::npos) {
            // Show first 6 bytes of acc as hex so we can see what's arriving
            if (s_status_cb && tries % 50 == 2) {
                char diag[32];
                snprintf(diag, sizeof(diag), "B:%02X%02X%02X%02X%02X%02X %zu",
                    acc[0], acc.size()>1?acc[1]:0, acc.size()>2?acc[2]:0,
                    acc.size()>3?acc[3]:0, acc.size()>4?acc[4]:0, acc.size()>5?acc[5]:0,
                    acc.size());
                s_status_cb(diag);
            }
            continue;
        }

        size_t resp_start = (h1 == std::string::npos) ? icy :
                            (icy == std::string::npos) ? h1 : std::min(h1, icy);

        // Find end of headers
        auto hdr_end = as.find("\r\n\r\n", resp_start);
        if (hdr_end == std::string::npos) continue;   // headers not fully received yet

        std::string headers = as.substr(resp_start, hdr_end - resp_start);
        ESP_LOGI(TAG, "stream: headers: %.120s", headers.c_str());

        // Handle redirect
        if (headers.find("301 ") != std::string::npos ||
            headers.find("302 ") != std::string::npos) {
            auto loc = headers.find("Location: ");
            if (loc != std::string::npos) {
                loc += 10;
                auto le = headers.find("\r\n", loc);
                std::string nurl = headers.substr(loc, le - loc);
                ESP_LOGI(TAG, "stream: redirect → %s", nurl.c_str());
                send_at_raw("AT+CIPCLOSE\r\n", 2000);
                return stream_open_url(nurl.c_str(), redirect_depth + 1);
            }
        }

        // Reject non-200 responses (404, 503, etc.)
        bool is_200 = headers.find("200 ") != std::string::npos ||
                      headers.find("200\r") != std::string::npos;
        if (!is_200) {
            // Extract just the status line for the error message
            auto crlf = headers.find("\r\n");
            std::string status = (crlf != std::string::npos) ?
                                 headers.substr(0, crlf) : headers.substr(0, 40);
            snprintf(s_stream_error, sizeof(s_stream_error), "%.40s", status.c_str());
            ESP_LOGE(TAG, "stream: bad status: %s", s_stream_error);
            send_at_raw("AT+CIPCLOSE\r\n", 2000);
            return nullptr;
        }

        // Store audio bytes after headers, stripping any +IPD framing boundaries.
        // acc may contain multiple +IPD packets: bytes from audio_start to the
        // next \r\n+IPD, are clean (within the same TCP packet as the headers),
        // everything after that needs +IPD frame stripping via raw/extract_ipd.
        size_t audio_start = hdr_end + 4;
        s_stream_obj.body.clear();
        s_stream_obj.raw.clear();
        if (audio_start < acc.size()) {
            // Search for the next +IPD boundary after audio_start
            const uint8_t *p = acc.data() + audio_start;
            size_t rem = acc.size() - audio_start;
            size_t clean_end = rem;  // assume all clean by default
            for (size_t k = 0; k + 6 < rem; k++) {
                if (p[k] == '\r' && p[k+1] == '\n' &&
                    p[k+2] == '+' && p[k+3] == 'I' &&
                    p[k+4] == 'P' && p[k+5] == 'D' && p[k+6] == ',') {
                    clean_end = k;
                    break;
                }
            }
            // Clean bytes go directly to body (no +IPD prefix in first TCP packet)
            s_stream_obj.body.assign(acc.begin() + audio_start,
                                     acc.begin() + audio_start + clean_end);
            // Remaining framed packets go to raw for extract_ipd
            if (clean_end < rem)
                s_stream_obj.raw.assign(acc.begin() + audio_start + clean_end, acc.end());
        }
        s_stream_obj.connected = true;
        ESP_LOGI(TAG, "stream: open, %zu body bytes buffered", s_stream_obj.body.size());
        return &s_stream_obj;
    }

    ESP_LOGE(TAG, "stream: header timeout (acc=%zu)", acc.size());
    snprintf(s_stream_error, sizeof(s_stream_error), "HEADER TIMEOUT");
    send_at_raw("AT+CIPCLOSE\r\n", 2000);
    return nullptr;
}

extern "C" wifi_at_stream_t wifi_at_stream_open(const char *url, stream_status_fn_t status_cb) {
    s_stream_obj = { false, {}, {} };
    s_status_cb = status_cb;
    wifi_at_stream_t s = stream_open_url(url, 0);
    if (!s) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        s = stream_open_url(url, 0);
    }
    s_status_cb = nullptr;
    return s;
}

extern "C" int wifi_at_stream_read(wifi_at_stream_t s, uint8_t *buf, size_t len, int timeout_ms) {
    if (!s || !s->connected || !s_at) return -1;

    // Serve buffered body bytes first (binary-safe)
    if (!s->body.empty()) {
        size_t n = std::min(len, s->body.size());
        memcpy(buf, s->body.data(), n);
        s->body.erase(s->body.begin(), s->body.begin() + n);
        return (int)n;
    }

    // Poll for +IPD data — unconditional ReceivePacket regardless of IRQ bit.
    // C6 does not reliably re-assert bit-23 after ClearIrqFlag when streaming.
    // When data is flowing we chain reads back-to-back (no inter-read delay) so
    // SDIO throughput isn't artificially limited to 480 bytes per 20 ms.
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        uint32_t flag = s_at->GetIrqFlag();
        if (s_at->ParseRxNewPacketFlag(flag)) {
            s_at->ClearIrqFlag(flag);
            vTaskDelay(pdMS_TO_TICKS(5));  // settle after IRQ clear
        }

        std::vector<uint8_t> pkt;
        if (s_at->ReceivePacket(pkt) && !pkt.empty()) {
            s->raw.insert(s->raw.end(), pkt.begin(), pkt.end());

            // Disconnect check
            {
                std::string rs(s->raw.begin(),
                               s->raw.begin() + std::min(s->raw.size(), (size_t)64));
                if (rs.find("CLOSED") != std::string::npos ||
                    rs.find("ERROR\r\n") != std::string::npos) {
                    ESP_LOGW(TAG, "stream: disconnect");
                    s->connected = false;
                    return -1;
                }
            }

            extract_ipd(s->raw, s->body);

            if (!s->body.empty()) {
                size_t n = std::min(len, s->body.size());
                memcpy(buf, s->body.data(), n);
                s->body.erase(s->body.begin(), s->body.begin() + n);
                return (int)n;
            }
            // Got SDIO data but extract_ipd needs more to complete a frame —
            // loop immediately (no delay) to chain the next read.
            continue;
        }

        // No data from C6 right now — wait before retrying
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }
    return 0;  // timeout, no data yet
}

extern "C" void wifi_at_stream_close(wifi_at_stream_t s) {
    if (!s) return;
    s->connected = false;
    s->raw.clear();
    s->body.clear();
    send_at_raw("AT+CIPCLOSE\r\n", 2000);
}

extern "C" void wifi_at_scan(wifi_scan_result_cb_t cb) {
    if (!s_at || !cb) return;

    // Use send_at_raw directly — the driver's WifiScan() strips +CWLAP: prefixes
    // inconsistently and uses IRQ-only polling.  send_at_raw gives us the full
    // AT response with every +CWLAP: tag intact.
    std::string resp = send_at_raw("AT+CWLAP\r\n", 10000);
    ESP_LOGI(TAG, "CWLAP raw (%d bytes): %.200s", (int)resp.size(), resp.c_str());

    // Parse lines like: +CWLAP:(ecn,"ssid",rssi,"bssid",channel,...)
    size_t pos = 0;
    while ((pos = resp.find("+CWLAP:(", pos)) != std::string::npos) {
        pos += 8;  // skip "+CWLAP:("
        // ecn digit then comma
        auto comma1 = resp.find(',', pos);
        if (comma1 == std::string::npos) break;
        // SSID in quotes
        auto q1 = resp.find('"', comma1 + 1);
        if (q1 == std::string::npos) break;
        auto q2 = resp.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string ssid = resp.substr(q1 + 1, q2 - q1 - 1);
        // RSSI after ssid's closing quote + comma
        auto comma2 = resp.find(',', q2 + 1);
        if (comma2 == std::string::npos) break;
        int rssi = atoi(resp.c_str() + comma2 + 1);
        if (!ssid.empty()) {
            cb(ssid.c_str(), rssi);
        }
        pos = comma2;
    }
}

extern "C" bool wifi_at_connect(const char *ssid, const char *pass) {
    if (!s_at || !ssid) return false;

    if (!s_at->SetWifiConnect(std::string(ssid), pass ? std::string(pass) : "", 15000)) {
        ESP_LOGE(TAG, "wifi_at_connect: SetWifiConnect failed for '%s'", ssid);
        return false;
    }

    // Refresh cached IP
    std::string cifsr = send_at_raw("AT+CIFSR\r\n", 3000);
    auto p = cifsr.find("STAIP,\"");
    if (p != std::string::npos) {
        p += 7;
        auto e = cifsr.find('"', p);
        if (e != std::string::npos) {
            std::string ip = cifsr.substr(p, e - p);
            strncpy(s_ip, ip.c_str(), sizeof(s_ip) - 1);
            s_ip[sizeof(s_ip) - 1] = '\0';
            ESP_LOGI(TAG, "New WiFi IP: %s", s_ip);
        }
    }
    return true;
}

extern "C" void wifi_at_reconnect(void) {
    if (s_at) s_at->Reconnect();
}

extern "C" const char *wifi_at_stream_last_error(void) {
    return s_stream_error;
}
