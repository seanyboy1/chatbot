#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// ── Pin assignments ───────────────────────────────────────────────────────────
#define SX1262_PIN_MOSI   3
#define SX1262_PIN_MISO   4
#define SX1262_PIN_SCLK   2
#define SX1262_PIN_CS    24
#define SX1262_PIN_BUSY   6
// RST and DIO1 are on XL9535 I2C expander — not used; polling BUSY instead.

// ── LoRa config defaults (changeable before sx1262_init) ─────────────────────
#define SX1262_FREQ_HZ    915000000UL   // 915 MHz (change to 868000000 for EU)
#define SX1262_SF         7             // Spreading factor 7–12
#define SX1262_BW         7             // 0=7.8k 1=10.4k 2=15.6k 3=20.8k 4=31.25k
                                        // 5=41.7k 6=62.5k 7=125k 8=250k 9=500k
#define SX1262_CR         1             // Coding rate: 1=4/5 2=4/6 3=4/7 4=4/8
#define SX1262_TX_POWER  14             // dBm (max 22 for SX1262)
#define SX1262_SYNC_WORD  0x1424        // 0x3444=LoRaWAN public, 0x1424=private

#define SX1262_MAX_PAYLOAD 255

// ── Packet info returned with each received frame ────────────────────────────
typedef struct {
    int8_t  rssi;   // dBm
    int8_t  snr;    // dB (signed, /4 for actual SNR)
    uint8_t len;
} sx1262_pkt_info_t;

// ── Public API ────────────────────────────────────────────────────────────────
esp_err_t sx1262_init(void);
esp_err_t sx1262_send(const uint8_t *data, uint8_t len);
// Returns ESP_OK + fills buf/info on received packet, ESP_ERR_NOT_FOUND if nothing pending.
esp_err_t sx1262_poll_rx(uint8_t *buf, uint8_t *len_out, sx1262_pkt_info_t *info);
// Enter continuous RX (call once after init or after a TX).
esp_err_t sx1262_start_rx(void);
// Read chip status byte.
uint8_t   sx1262_get_status(void);
