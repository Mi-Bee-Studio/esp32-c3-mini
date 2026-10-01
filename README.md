# ESP32-S3-Mini (Lolin S3 Mini Mainboard)

[中文文档](README.zh.md) | [English](README.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/esp32-s3-mini/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/esp32-s3-mini/actions/workflows/build.yml)

A board under the board-centric repo convention. **This repo is organized with the board as root:**

```
esp32-s3-mini/
├── README.md          # this file: all hardware info for this board
└── <project>/         # one directory per project built on this board (named by capability)
    ├── CMakeLists.txt / main/ 
    └── README.md      # project description + build/flash commands
```

Key points of the convention:

- **Board directory name** = board name (kebab-case); the root README covers hardware only, never project content;
- **Each project directory builds standalone**: it ships its full build trio; `cd <project> && idf.py build` produces the firmware;
- Projects share no code; when commonality is needed, copy first, and consider extracting a shared component only once things stabilize.

### Firmware baseline norms (mandatory fleet-wide)

1. **Watchdog: mandatory.** Tasks subscribe to the ESP-IDF TWDT and feed it periodically;
2. **Web/API firmware upgrade (OTA): mandatory where the hardware allows.** WiFi plus 4 MB flash fit dual OTA slots, so every project ships a web upgrade path.

| Project | Watchdog | Web/API OTA |
|---------|----------|-------------|
| blink | ✅ per-task TWDT (5 s panic) | ✅ dual OTA slots + streaming `POST /ota` |

---

## Board Overview

| Item | Value |
|------|-------|
| Module | **ESP32-S3FH4R2** — Xtensa LX7 dual-core @ 240 MHz (in-package 4 MB flash + 2 MB Octal PSRAM) |
| Flash | 4 MB (embedded) |
| PSRAM | 2 MB Octal (embedded; **unverified in this repo** — validate on hardware before enabling `SPIRAM`; see the PSRAM lesson in the `esp32-s3-zero` root README) |
| Wireless | 2.4 GHz WiFi b/g/n + BLE 5 |
| USB | Native USB Type-C (USB OTG / **USB-Serial-JTAG**: console + flashing, no bridge chip) |
| Onboard LED | **WS2812 RGB = GPIO47** (addressable) |
| Buttons | RESET + BOOT (**BOOT GPIO not documented on the official page** — hold BOOT while plugging USB for download mode) |
| Dimensions | 34.3 × 25.4 mm |
| Source | Board facts from the [Wemos/Lolin official page](https://www.wemos.cc/en/latest/s3/s3_mini.html) — the most common board called "esp32-s3-mini"; **if your carrier board is a different vendor, trust the actual board** |

## Pinout Diagram (USB-C pointing up, front/component-side view; functional pin map)

```
                 ┌─ USB-C ─┐
                 │ [WS2812]│
                 │  =IO47  │   ESP32-S3FH4R2
                 │ [RST]   │   4MB flash + 2MB Octal PSRAM (in-package)
                 │ [BOOT]  │   27× IO on two header rows
                 └─────────┘
  The physical pad order is not documented as a table on the official
  page — follow the official wemos.cc pinout diagram for wiring; do not
  infer wiring from this figure.
```

Key points:

- GPIO33–37 are taken by the Octal PSRAM (generic S3 constraint);
- GPIO19/20 = USB D-/D+ — do not repurpose; GPIO0 = BOOT (power-on level selects boot mode, not stated on the official page — verify on hardware);
- WS2812 sits on GPIO47 (used by this repo's blink).

## Caveats

- **No USB-UART bridge**: the serial port is the S3's USB-Serial-JTAG;
- **PSRAM unverified**: the S3FH4R2 ships 2 MB Octal PSRAM, but the sibling esp32-s3-zero board had a PSRAM init hang — this repo's firmware ships `CONFIG_SPIRAM=n` until validated;
- BOOT button GPIO not documented — download mode: hold BOOT while plugging USB / pressing RESET.

## Project Index

| Project | Description |
|---------|-------------|
| [blink](blink/README.md) | Baseline/test firmware: WS2812 (GPIO47) status-color rotation + web maintenance page (provisioning/OTA) + TWDT watchdog |
