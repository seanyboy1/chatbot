#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <time.h>
#include <stdbool.h>

/* Callback to assert/deassert C6_EN via XL9535 (true = powered, false = reset) */
typedef void (*c6_en_fn_t)(bool high);

/*
 * Initialize SDIO-AT link to C6 AT firmware and connect to WiFi.
 * Blocks until WiFi connects or fails (~15 s worst case).
 * Returns true on success.
 */
bool wifi_at_init(c6_en_fn_t en_fn, const char *ssid, const char *pass);

/* True if WiFi is currently up */
bool wifi_at_is_connected(void);

/* Copy current IP string into buf (empty string if not connected) */
void wifi_at_get_ip(char *buf, size_t len);

/*
 * SNTP time from C6 AT firmware.
 * Returns UNIX epoch (UTC), or 0 if not available.
 */
time_t wifi_at_get_epoch(void);

/*
 * HTTP GET via AT+HTTPCLIENT.  Allocates *out_buf on the heap (caller frees).
 * Returns body length (>= 0) or -1 on error.
 * Supports http:// and https://.
 */
int wifi_at_http_get(const char *url, char **out_buf);

/* Force a WiFi reconnect (used by the UI reconnect button) */
void wifi_at_reconnect(void);

/*
 * Scan for nearby WiFi networks.
 * Calls cb(ssid, rssi) for each result found.
 * Blocks for up to ~8 seconds.
 */
typedef void (*wifi_scan_result_cb_t)(const char *ssid, int rssi);
void wifi_at_scan(wifi_scan_result_cb_t cb);

/*
 * Connect to a new WiFi network.
 * Blocks up to ~15 seconds.  Returns true on success.
 * On success, updates the IP returned by wifi_at_get_ip().
 */
bool wifi_at_connect(const char *ssid, const char *pass);

/* Last AT error string from a failed stream open (static buffer, valid until next call) */
const char *wifi_at_stream_last_error(void);

/*
 * HTTP audio streaming via AT transparent TCP/SSL mode.
 * wifi_at_stream_open: open URL, skip HTTP headers, return opaque handle or NULL.
 * wifi_at_stream_read: read up to len bytes of body into buf; returns bytes read,
 *                      0 if no data yet, -1 on error/disconnect.
 * wifi_at_stream_close: stop stream and release connection.
 */
typedef struct wifi_at_stream_s *wifi_at_stream_t;
typedef void (*stream_status_fn_t)(const char *msg);
wifi_at_stream_t wifi_at_stream_open(const char *url, stream_status_fn_t status_cb);
int  wifi_at_stream_read(wifi_at_stream_t s, uint8_t *buf, size_t len, int timeout_ms);
void wifi_at_stream_close(wifi_at_stream_t s);

#ifdef __cplusplus
}
#endif
