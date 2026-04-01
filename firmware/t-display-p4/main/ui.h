#pragma once
#include "lvgl.h"
#include <stdbool.h>

// Create the cable-tester UI (call once after lv_init)
void ui_init(void);

// Update cable test result:
//   linked=true  → big green PASS + speed + duplex info
//   linked=false → big red FAIL + fault hint
void ui_set_eth_result(bool linked, int speed_mbps, bool full_duplex);

// Update the header status string (WiFi state, boot messages)
void ui_set_status(const char *msg);

// Show the device WiFi IP in the header
void ui_set_wifi_ip(const char *ip);
