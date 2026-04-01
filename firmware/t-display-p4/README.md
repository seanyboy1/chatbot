# BLUE-NET ETH Cable Tester — T-Display P4

Turns the LilyGo T-Display P4 (ESP32-P4) into a standalone Ethernet cable tester.
Plug a cable into the ETH port: the screen shows **PASS / FAIL**, link speed, and duplex.
Results also stream to your BLUE-NET dashboard over WiFi.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-P4 |
| Display | 800×480 RGB LCD (ST7262) |
| ETH PHY | LAN8720A (RMII) |
| WiFi | ESP32-C6 companion (same module) |

---

## Prerequisites

- **ESP-IDF v5.4+** — [Install guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/get-started/)
- USB-C cable to the T-Display P4

---

## 1 — Configure

Open `main/main.c` and edit the three lines at the top:

```c
#define CONFIG_WIFI_SSID       "your-network"
#define CONFIG_WIFI_PASSWORD   "your-password"
#define CONFIG_BLUENET_SERVER  "http://192.168.1.100:3000"  // your server LAN IP
```

### GPIO pins

LCD and ETH GPIO assignments are in `main/main.c` under the `LCD GPIO` and
`ETHERNET PHY GPIO` sections. Defaults match the LilyGo T-Display-P4 schematic —
verify them before flashing:

```
https://github.com/Xinyuan-LilyGO/T-Display-P4
```

---

## 2 — Build

```bash
# From the firmware/t-display-p4/ directory:
. $HOME/esp/esp-idf/export.sh

idf.py set-target esp32p4
idf.py build
```

---

## 3 — Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your port (`/dev/tty.usbserial-*` on Mac, `COM3` on Windows).

> Hold the **BOOT** button while connecting USB if the device doesn't enter flash mode automatically.

---

## 4 — What to expect

| State | Screen |
|-------|--------|
| Boot | `CONNECTING WIFI...` in header |
| WiFi connected | IP address + green dot in top-right |
| WiFi failed | `WIFI FAILED — OFFLINE` (device still tests cables without WiFi) |
| Cable plugged in — link up | Big green **PASS** + speed + duplex |
| Cable plugged in — no link | Big red **FAIL** + fault hints |
| Cable unplugged | Returns to `-- --` idle state |

---

## File structure

```
firmware/t-display-p4/
├── CMakeLists.txt       # ESP-IDF project (name: eth-cable-tester)
├── idf_component.yml    # LVGL 9 dependency
├── sdkconfig.defaults   # Default Kconfig values
└── main/
    ├── CMakeLists.txt   # Component registration
    ├── main.c           # LCD init, WiFi, ETH tester, LVGL tasks, heartbeat
    ├── ui.h             # UI function declarations
    └── ui.c             # LVGL 9 cable-tester screen (800×480)
```

---

## Troubleshooting

**White / blank screen** — Check LCD GPIO pins against your board schematic.

**No ETH link on a known-good cable** — Verify `CONFIG_ETH_MDC_GPIO`, `CONFIG_ETH_MDIO_GPIO`, and `CONFIG_ETH_PHY_ADDR`.

**WiFi fails** — Double-check SSID/password. The tester works standalone without WiFi.

**Device not detected by `idf.py flash`** — Hold BOOT while plugging in USB.
