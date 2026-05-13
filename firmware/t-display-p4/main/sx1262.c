#include "sx1262.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "sx1262";

// ── SX1262 SPI commands ───────────────────────────────────────────────────────
#define CMD_SET_SLEEP               0x84
#define CMD_SET_STANDBY             0x80
#define CMD_SET_TX                  0x83
#define CMD_SET_RX                  0x82
#define CMD_SET_REGULATOR_MODE      0x96
#define CMD_SET_DIO2_RF_SWITCH      0x9D
#define CMD_SET_DIO3_TCXO           0x97
#define CMD_CALIBRATE               0x89
#define CMD_CALIBRATE_IMAGE         0x98
#define CMD_SET_PA_CONFIG           0x95
#define CMD_SET_RF_FREQUENCY        0x86
#define CMD_SET_PACKET_TYPE         0x8A
#define CMD_SET_TX_PARAMS           0x8E
#define CMD_SET_MODULATION_PARAMS   0x8B
#define CMD_SET_PACKET_PARAMS       0x8C
#define CMD_SET_BUFFER_BASE_ADDR    0x8F
#define CMD_SET_DIO_IRQ_PARAMS      0x08
#define CMD_GET_IRQ_STATUS          0x12
#define CMD_CLEAR_IRQ_STATUS        0x02
#define CMD_GET_STATUS              0xC0
#define CMD_GET_RX_BUFFER_STATUS    0x13
#define CMD_GET_PACKET_STATUS       0x14
#define CMD_WRITE_REGISTER          0x0D
#define CMD_READ_REGISTER           0x1D
#define CMD_WRITE_BUFFER            0x0E
#define CMD_READ_BUFFER             0x1E

// IRQ masks
#define IRQ_TX_DONE     (1 << 0)
#define IRQ_RX_DONE     (1 << 1)
#define IRQ_TIMEOUT     (1 << 9)

// SX1262 register for sync word (LoRa)
#define REG_LORA_SYNC_WORD_MSB  0x0740
#define REG_LORA_SYNC_WORD_LSB  0x0741

static spi_device_handle_t s_spi = NULL;

// ── Low-level helpers ─────────────────────────────────────────────────────────

static void wait_busy(void) {
    // BUSY high = chip processing; wait for it to go low (max ~3 s for calibration)
    int timeout = 30000;
    while (gpio_get_level(SX1262_PIN_BUSY) && --timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (timeout == 0) ESP_LOGW(TAG, "BUSY timeout");
}

static void spi_cmd(const uint8_t *tx, uint8_t *rx, int len) {
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_spi, &t);
}

// Send command with no response bytes needed.
static void cmd(uint8_t opcode, const uint8_t *params, int n) {
    wait_busy();
    uint8_t buf[32];
    buf[0] = opcode;
    if (params && n > 0) memcpy(buf + 1, params, n);
    spi_cmd(buf, NULL, 1 + n);
}

// Send command and read back n response bytes (skipping the status byte).
static void cmd_read(uint8_t opcode, uint8_t *out, int n) {
    wait_busy();
    uint8_t tx[34] = {opcode};
    uint8_t rx[34] = {0};
    spi_cmd(tx, rx, 2 + n);  // opcode + status byte + n data bytes
    memcpy(out, rx + 2, n);
}

static void write_reg(uint16_t addr, uint8_t val) {
    wait_busy();
    uint8_t buf[4] = { CMD_WRITE_REGISTER, addr >> 8, addr & 0xFF, val };
    spi_cmd(buf, NULL, 4);
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t sx1262_init(void) {
    // Configure BUSY as input
    gpio_config_t busy_cfg = {
        .pin_bit_mask = (1ULL << SX1262_PIN_BUSY),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_cfg);

    // Init SPI bus
    spi_bus_config_t bus = {
        .mosi_io_num   = SX1262_PIN_MOSI,
        .miso_io_num   = SX1262_PIN_MISO,
        .sclk_io_num   = SX1262_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 8 * 1000 * 1000,  // 8 MHz
        .mode           = 0,                  // SX1262: CPOL=0, CPHA=0
        .spics_io_num   = SX1262_PIN_CS,
        .queue_size     = 1,
    };
    ret = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Give chip a moment after power-on
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();

    // Ensure STANDBY_RC mode
    uint8_t standby_rc = 0x00;
    cmd(CMD_SET_STANDBY, &standby_rc, 1);

    // Use LDO regulator (no DCDC on this module)
    uint8_t reg_ldo = 0x00;
    cmd(CMD_SET_REGULATOR_MODE, &reg_ldo, 1);

    // DIO2 controls RF switch (HPD16A module has a built-in RF switch)
    uint8_t dio2_rf = 0x01;
    cmd(CMD_SET_DIO2_RF_SWITCH, &dio2_rf, 1);

    // Packet type: LoRa
    uint8_t pkt_lora = 0x01;
    cmd(CMD_SET_PACKET_TYPE, &pkt_lora, 1);

    // RF frequency
    uint32_t freq_word = (uint32_t)((double)SX1262_FREQ_HZ / 32000000.0 * (1 << 25));
    uint8_t freq[4] = { freq_word >> 24, (freq_word >> 16) & 0xFF,
                        (freq_word >>  8) & 0xFF, freq_word & 0xFF };
    cmd(CMD_SET_RF_FREQUENCY, freq, 4);

    // PA config for SX1262: paDutyCycle=0x04, hpMax=0x07, deviceSel=0x00, paLut=0x01
    uint8_t pa[4] = { 0x04, 0x07, 0x00, 0x01 };
    cmd(CMD_SET_PA_CONFIG, pa, 4);

    // TX params: power, ramp time 200 us
    uint8_t tx_p[2] = { (uint8_t)SX1262_TX_POWER, 0x04 };
    cmd(CMD_SET_TX_PARAMS, tx_p, 2);

    // Modulation params: SF, BW, CR, LDRO
    // LDRO should be 1 if symbol time > 16.38 ms (SF11+BW125 or SF12+BW125/250)
    uint8_t ldro = (SX1262_SF >= 11 && SX1262_BW <= 7) ? 1 : 0;
    uint8_t mod[4] = { SX1262_SF, SX1262_BW, SX1262_CR, ldro };
    cmd(CMD_SET_MODULATION_PARAMS, mod, 4);

    // Packet params: preamble=12, variable header, max payload, CRC on, no IQ invert
    uint8_t pkt[6] = { 0x00, 12, 0x00, SX1262_MAX_PAYLOAD, 0x01, 0x00 };
    cmd(CMD_SET_PACKET_PARAMS, pkt, 6);

    // Buffer base: TX at 0x00, RX at 0x80
    uint8_t buf_base[2] = { 0x00, 0x80 };
    cmd(CMD_SET_BUFFER_BASE_ADDR, buf_base, 2);

    // Sync word (private network = 0x1424)
    write_reg(REG_LORA_SYNC_WORD_MSB, (SX1262_SYNC_WORD >> 8) & 0xFF);
    write_reg(REG_LORA_SYNC_WORD_LSB,  SX1262_SYNC_WORD       & 0xFF);

    // IRQ: enable TX_DONE + RX_DONE + TIMEOUT on DIO1
    uint8_t irq[8] = {
        0x02, 0x01,   // irqMask: TX_DONE | RX_DONE
        0x02, 0x01,   // DIO1 mask
        0x00, 0x00,   // DIO2 mask
        0x00, 0x00,   // DIO3 mask
    };
    cmd(CMD_SET_DIO_IRQ_PARAMS, irq, 8);

    ESP_LOGI(TAG, "SX1262 init OK — %lu Hz SF%d BW%d +%d dBm",
             (unsigned long)SX1262_FREQ_HZ, SX1262_SF, SX1262_BW, SX1262_TX_POWER);

    return sx1262_start_rx();
}

esp_err_t sx1262_start_rx(void) {
    // Continuous RX: timeout=0x000000 means no timeout
    uint8_t rx[3] = { 0x00, 0x00, 0x00 };
    cmd(CMD_SET_RX, rx, 3);
    return ESP_OK;
}

esp_err_t sx1262_send(const uint8_t *data, uint8_t len) {
    if (!data || len == 0 || len > SX1262_MAX_PAYLOAD) return ESP_ERR_INVALID_ARG;

    // Go to standby before writing buffer
    uint8_t stdby = 0x00;
    cmd(CMD_SET_STANDBY, &stdby, 1);

    // Update packet params with actual payload length
    uint8_t pkt[6] = { 0x00, 12, 0x00, len, 0x01, 0x00 };
    cmd(CMD_SET_PACKET_PARAMS, pkt, 6);

    // Write payload to TX buffer at base 0x00
    wait_busy();
    uint8_t wr_hdr[2] = { CMD_WRITE_BUFFER, 0x00 };
    uint8_t tx_buf[2 + SX1262_MAX_PAYLOAD];
    memcpy(tx_buf, wr_hdr, 2);
    memcpy(tx_buf + 2, data, len);
    spi_cmd(tx_buf, NULL, 2 + len);

    // Clear any stale IRQ
    uint8_t clr[2] = { 0xFF, 0xFF };
    cmd(CMD_CLEAR_IRQ_STATUS, clr, 2);

    // Start TX (timeout=0 = no timeout)
    uint8_t tx_args[3] = { 0x00, 0x00, 0x00 };
    cmd(CMD_SET_TX, tx_args, 3);

    // Wait for TX_DONE IRQ via BUSY polling (BUSY stays high during TX)
    wait_busy();

    // Clear TX done IRQ and return to RX
    cmd(CMD_CLEAR_IRQ_STATUS, clr, 2);
    return sx1262_start_rx();
}

esp_err_t sx1262_poll_rx(uint8_t *buf, uint8_t *len_out, sx1262_pkt_info_t *info) {
    // Check IRQ status for RX_DONE
    uint8_t irq[2] = {0};
    cmd_read(CMD_GET_IRQ_STATUS, irq, 2);
    uint16_t irq_flags = ((uint16_t)irq[0] << 8) | irq[1];

    if (!(irq_flags & IRQ_RX_DONE)) return ESP_ERR_NOT_FOUND;

    // Clear IRQ
    uint8_t clr[2] = { 0xFF, 0xFF };
    cmd(CMD_CLEAR_IRQ_STATUS, clr, 2);

    // Timeout without data
    if (irq_flags & IRQ_TIMEOUT) {
        sx1262_start_rx();
        return ESP_ERR_NOT_FOUND;
    }

    // Get buffer status: [payloadLen, startOffset]
    uint8_t bstat[2] = {0};
    cmd_read(CMD_GET_RX_BUFFER_STATUS, bstat, 2);
    uint8_t plen   = bstat[0];
    uint8_t offset = bstat[1];

    if (plen == 0 || plen > SX1262_MAX_PAYLOAD) {
        sx1262_start_rx();
        return ESP_ERR_NOT_FOUND;
    }

    // Read payload from RX buffer
    wait_busy();
    uint8_t rd_tx[3 + SX1262_MAX_PAYLOAD] = { CMD_READ_BUFFER, offset, 0x00 };
    uint8_t rd_rx[3 + SX1262_MAX_PAYLOAD] = {0};
    spi_cmd(rd_tx, rd_rx, 3 + plen);
    memcpy(buf, rd_rx + 3, plen);
    *len_out = plen;

    // Packet status: RSSI, SNR
    if (info) {
        uint8_t pstat[3] = {0};
        cmd_read(CMD_GET_PACKET_STATUS, pstat, 3);
        info->rssi = -(pstat[0] >> 1);         // rssiPkt / 2
        info->snr  = (int8_t)pstat[1];         // snrPkt (signed, actual = /4)
        info->len  = plen;
    }

    sx1262_start_rx();
    return ESP_OK;
}

uint8_t sx1262_get_status(void) {
    uint8_t status = 0;
    cmd_read(CMD_GET_STATUS, &status, 0);
    // status is in rx[1] for GET_STATUS — re-read correctly
    wait_busy();
    uint8_t tx[2] = { CMD_GET_STATUS, 0x00 };
    uint8_t rx[2] = {0};
    spi_cmd(tx, rx, 2);
    return rx[1];
}
