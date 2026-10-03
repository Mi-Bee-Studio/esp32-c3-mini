# weather-clock — ST7735 TFT + AHT30 desktop clock

> Ported from the user's Arduino sketch `esp32c3-pro-0.96-aht30` (same wiring) to ESP-IDF.

[中文文档](README.zh.md)

## What it does

- **ST7735 0.96" 160x80 mini TFT** (hardware SPI, rotation 3 landscape): NTP clock
  refreshed every second (UTC+8, LAN NTP `192.168.63.1` with `pool.ntp.org`
  fallback; shows `--:--:--` until synced);
- **AHT30 temp/humidity** (I2C 0x38): read every 5 s, painted over a **random
  background color** (64..255 per channel) with auto black/white contrast text —
  same look as the Arduino sketch (time/temp/humidity rows at 2x + date row at 1x);
- **Onboard web maintenance page :80**: WiFi provisioning / firmware OTA / reboot
  (rescue SoftAP `wclock-c3m` / `12345678` → 192.168.4.1 when unprovisioned);
- **Watchdog**: ESP-IDF TWDT, 5 s timeout with panic (main loop feeds every 1 s);
- **OTA**: dual OTA slots (app at `0x20000`); the web page streams to the
  alternate slot → verify → switch → reboot.

## Wiring (carried over from the Arduino sketch `app_config.h`)

| Signal | GPIO | Note |
|--------|------|------|
| TFT SCLK | 4 | |
| TFT MOSI | 6 | |
| TFT CS   | 7 | also drives the onboard WS2812 DIN — the LED may flicker randomly when CS toggles (expected, harmless) |
| TFT DC   | 5 | |
| TFT RST  | 3 | |
| TFT BL   | 10 | active high |
| AHT30 SDA | 8 | C3 strapping (must be high at boot); module pull-up satisfies it |
| AHT30 SCL | 9 | BOOT button shares it — pressing BOOT pulls SCL low and fails that read; ignored by design |

Panel init replicates Adafruit `initR(INITR_MINI160x80)` (Rcmd1 +
Rcmd2green160x80 + Rcmd3 + RGB-filter MADCTL); rotation 3 = `MADCTL 0x60`,
frame offsets X=0 / Y=24 (windowed out of the 132x162 panel RAM). Drawing uses
`esp_lcd` panel_io synchronous transactions + a 160-pixel line buffer — no full
framebuffer (C3 has no PSRAM).

## Build & Flash

```bash
cd weather-clock
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor   # console is the native USB-Serial-JTAG
```

## Gotchas (read before porting)

- **`esp_lcd`'s `trans_queue_depth` must be ≥ 1**: it becomes the spi device
  transaction queue length; 0 (the old "synchronous" semantics) crashes in
  `xQueueCreate(0,…)` assert (queue.c:573);
- **IDF v6 initializes the TWDT during startup**: calling `esp_task_wdt_init`
  again logs `already initialized`; use `esp_task_wdt_reconfigure` instead;
- **AHT30 retries must be idempotent**: calling `i2c_new_master_bus` twice hits
  `bus already acquired`; after a successful init only retry `read`;
- **RF caveat**: the Lolin C3 Mini's PCB antenna sits on the board edge — a
  fully-populated breadboard, jumper wires over it, or a metal desk nearby
  degrades the radio badly. On a breadboard this board's rescue AP was invisible
  and STA auth timed out (`auth -> init (0x200)`); the Arduino firmware failed
  the same way in the same spot. Keep the antenna region clear.

## Firmware baseline

| Baseline | Status |
|----------|--------|
| Watchdog | ✅ TWDT 5 s panic, main loop feeds |
| Web/API OTA | ✅ dual OTA slots + streaming `POST /ota` |
