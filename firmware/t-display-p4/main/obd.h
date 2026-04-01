#pragma once
#include <stdint.h>
#include <stdbool.h>

#define OBD_HOST_DEFAULT  "192.168.0.10"
#define OBD_PORT_DEFAULT  35000
#define OBD_TIMEOUT_MS    3000
#define OBD_MAX_RESP      128

typedef struct {
    float rpm;
    float speed_mph;
    float coolant_f;
    float iat_f;
    float throttle_pct;
    float fuel_pct;
    float battery_v;
    float timing_deg;
    float engine_load_pct;
    bool  connected;
} obd_data_t;

// Initialize OBD TCP connection to ELM327 WiFi adapter
bool obd_connect(const char *host, uint16_t port);

// Run ELM327 init sequence (ATZ, ATE0, etc.)
bool obd_init(const char *protocol);

// Poll all PIDs once — updates the obd_data_t struct
bool obd_poll(obd_data_t *data);

// Disconnect
void obd_disconnect(void);

// Send raw AT/OBD command, return response in buf
bool obd_send(const char *cmd, char *buf, size_t buf_len);
