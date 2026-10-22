# ESP32-C3-Mini (nologo ESP32-C3 SuperMini Mainboard)

[中文文档](README.zh.md) | [English](README.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/esp32-c3-mini/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/esp32-c3-mini/actions/workflows/build.yml)

<img src="https://www.nologo.tech/assets/img/esp32/esp32c3supermini/esp32c3.png" alt="ESP32-C3 SuperMini" width="420">

A board under the board-centric repo convention. **This repo is organized with the board as root:**

```
esp32-c3-mini/
├── README.md          # this file: all hardware info for this board
└── <project>/         # one directory per project built on this board (named by capability)
    ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
    └── README.md      # project description + build/flash commands
```

Key points of the convention:

- **Board directory name** = board name (kebab-case); the root README covers hardware only, never project content;
- **Each project directory builds standalone**: it ships the full ESP-IDF project trio (top-level CMakeLists,
  `main/`, `sdkconfig.defaults`); `cd <project> && idf.py build` produces the firmware;
- Projects share no code; when commonality is needed, copy first, and consider extracting a shared component only once things stabilize.

### Firmware baseline norms (mandatory fleet-wide)

1. **Watchdog: mandatory.** Tasks subscribe to the ESP-IDF TWDT and feed it periodically;
2. **Web/API firmware upgrade (OTA): mandatory where the hardware allows.** WiFi plus
   4 MB flash fit dual OTA slots, so every project ships a web upgrade path.

| Project | Watchdog | Web/API OTA |
|---------|----------|-------------|
| blink | ✅ per-task TWDT (5 s panic) | ✅ dual OTA slots + streaming `POST /ota` |
| weather-clock | ✅ per-task TWDT (5 s panic) | ✅ dual OTA slots + streaming `POST /ota` |
| env-csi-station | ✅ per-task TWDT (5 s panic) | ✅ dual OTA slots + streaming `POST /ota` |

---

## Board Overview

| Item | Value |
|------|-------|
| Module/chip | **ESP32-C3FN4** — RISC-V single-core @ 160 MHz (4 MB flash in-package, no PSRAM) |
| Flash | 4 MB (in-package) |
| SRAM | 400 KB (+ 384 KB ROM) |
| PSRAM | None |
| Wireless | 2.4 GHz WiFi b/g/n + Bluetooth 5 (LE) |
| USB | Native USB Type-C — **ROM USB-CDC** (console + flashing; no bridge chip, no USB-Serial-JTAG peripheral) |
| Onboard LED | **Blue LED @ GPIO8, active-LOW** (VSYS→5.1k→LED→GPIO8 with a 10k pull-up; plus a red power LED) |
| Buttons | BOOT = GPIO9 (10k pull-up; hold + RESET for download mode), RESET |
| Power | USB 5V, or external 3.3–6V on the 5V pad (ME6211 LDO; **USB and external power are mutually exclusive**) |
| Antenna | Onboard PCB antenna (external antenna option) |
| Deep sleep | ~43 µA |
| Dimensions | 22.52 × 18 mm, single-sided SMT |
| Breakout | 11× GPIO (GPIO0-10) + GPIO20/21 (UART0), 4× ADC |
| Source | [nologo product page](https://www.nologo.tech/product/esp32) (pin usage also on the [Foot/Pin page](https://www.nologo.tech/esp32C3SuperMiniFoot)); board identity corrected 2026-10-22 from a physical unit (previously mis-documented as Lolin C3 Mini) |

## Pinout Diagram (USB-C pointing up, front/component-side view)

```
                 ┌── USB-C ──┐
  left (top→down)│  ME6211   │ right (top→down)
        5V ─────┤5V      GPIO5├──── A5 / MISO
        G  ─────┤G       GPIO6├──── MOSI
       3V3 ─────┤3V3     GPIO7├──── SS
      GPIO4 ────┤A4/SCK  GPIO8├──── SDA  [blue LED=8, active-LOW] strapping
      GPIO3 ────┤A3      GPIO9├──── SCL  [BOOT=9] strapping
      GPIO2 ────┤A2     GPIO10├────
      GPIO1 ────┤A1     GPIO20├──── U0RXD
      GPIO0 ────┤A0     GPIO21├──── U0TXD
                 │ ESP32-C3FN4 │   PCB antenna
                 └─ 22.52×18mm─┘   [RST] dedicated reset button
```

Key points:

- Default I2C: SDA=**GPIO8** / SCL=**GPIO9** (shared with the blue LED/BOOT button — see Caveats);
- Default SPI: SCK=GPIO4 · MISO=GPIO5 · MOSI=GPIO6 · SS=GPIO7;
- ADC: A0–A5 = GPIO0–5; UART0: TX=GPIO21 · RX=GPIO20;
- Strapping pins: **GPIO2/8/9** (GPIO5 also VDD_SPI-related on some modules) — mind power-on levels;
- **GPIO18/19 = USB D±** (not broken out — do not repurpose); GPIO11-17 are internal flash.

## Caveats

- **The console is ROM USB-CDC**: sdkconfig needs `CONFIG_ESP_CONSOLE_USB_CDC=y`
  (blink/weather-clock still use the USB_SERIAL_JTAG option, which does not work on
  this board — a known issue; env-csi-station is fixed, the older projects were left
  untouched this round);
- Download mode: **hold BOOT (GPIO9) + tap RESET**; some cables/hosts need a re-plug retry;
- **USB and external power are mutually exclusive** (no back-feed; don't plug USB while
  powering from the 5V pad);
- **GPIO8 = onboard blue LED** (active-LOW with 10k pull-up): as I2C SDA it flickers
  faintly with traffic (harmless); also a strapping pin — peripherals must not pull it
  low at reset;
- **GPIO9 = BOOT button**: doubles as SCL; pressing BOOT mid-run breaks one I2C read
  (retried by firmware) — don't keep it pressed;
- No PSRAM — plan large buffers around the ~400 KB SRAM budget;
- The factory firmware is an Arduino-ecosystem MicroPython build — flashing ESP-IDF
  replaces it (re-flashable).

## Project Index

| Project | Description |
|---------|-------------|
| [blink](blink/README.md) | Baseline/test firmware: WS2812 status-color rotation + BOOT interaction + web maintenance page (provisioning/OTA) + TWDT watchdog (WS2812@GPIO7 per the old Lolin doc — this board has no such LED, pending fix) |
| [weather-clock](weather-clock/README.md) | ST7735 0.96" TFT (160x80) + AHT30 desktop clock: NTP time, temp/hum over a rotating random background + web provisioning/OTA (I2C 8/9) |
| [env-csi-station](env-csi-station/README.md) | AHT20 temp/humidity (hardware I2C 8/9) + WiFi CSI capture (1 Hz gateway ping ACK stimulus) + SSD1306 display (soft I2C 4/6) + web provisioning/OTA |
