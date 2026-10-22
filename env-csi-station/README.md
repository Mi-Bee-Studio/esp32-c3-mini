# env-csi-station — AHT20 Environment + WiFi CSI Station

[中文文档](README.zh.md) | [English](README.md)

A desktop sensor station on the ESP32-C3 SuperMini (nologo, C3FN4):
**AHT20 temperature/humidity** (hardware I2C0) + **WiFi CSI capture**
(after association a 1 Hz gateway ping stimulates samples — ACK frames
carry CSI; see the header comment in main/app_csi.c) + **SSD1306 128x64
display** (software I2C), with web provisioning and dual-slot OTA. Sibling
sources: blink (web/OTA baseline), esp32-s2-mini/env-station (AHT20 driver),
esp32-s3-zero/env-station (SSD1306 driver), and
luatos-esp32c3/air101-lcd/wifi-csi-sensing (CSI config + ping stimulus).

## Wiring

| Peripheral | Pin | Notes |
|------------|-----|-------|
| AHT20 SDA | **GPIO8** (I2C0 SDA) | GPIO8 doubles as the onboard blue LED (active-LOW, 10k pull-up): it flickers faintly with I2C traffic — normal, do not add a strong pull-down; also a strapping pin, keep high at reset |
| AHT20 SCL | **GPIO9** (I2C0 SCL) | GPIO9 = BOOT button: holding it mid-run breaks one I2C read (firmware retries); do not keep it pressed |
| AHT20 VCC/GND | 3V3 / G | 3.3 V supply |
| SSD1306 SDA | **GPIO4** (soft I2C) | Bit-banged soft I2C (~250 kHz, write path only) |
| SSD1306 SCL | **GPIO6** (soft I2C) | — |
| SSD1306 VCC/GND | 3V3 / G | Address **0x3C**; module needs pull-ups (most 4-pin modules carry 4.7-10k; without them the driver reports no-pull-up and skips refresh) |

**Why two buses**: the ESP32-C3 has a single hardware I2C controller; I2C0 is
taken by the AHT20 (8/9), so the SSD1306 runs over bit-banged soft I2C (4/6) —
the two peripherals never share wires.

## Build (activate IDF v6.0 in PowerShell)

```powershell
# Activate the local IDF v6.0 environment (adjust to your install paths)
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$env:IDF_PATH='C:\Users\micke\esp\v6.0\esp-idf'
$env:IDF_TOOLS_PATH='C:\Espressif\tools'
. C:\Users\micke\esp\v6.0\esp-idf\export.ps1
Set-Location D:\Projects\embedded\esp32-c3-mini\env-csi-station
idf.py set-target esp32c3
idf.py build
```

Console and flashing go over **ROM USB-CDC** (this board has no
USB-Serial-JTAG peripheral; sdkconfig.defaults sets
CONFIG_ESP_CONSOLE_USB_CDC=y). Flashing via the serialtap proxy:

```bash
serialtap flash <device> \
  <abs>bootloader.bin@0x0 <abs>partition-table.bin@0x8000 \
  <abs>ota_data_initial.bin@0x10000 <abs>env_csi_station.bin@0x20000 \
  --esptool C:/Espressif/tools/python/v6.0/venv/Scripts/esptool.exe \
  --chip esp32c3 --baud 460800    # bins must be absolute paths
```

## Provisioning & OTA

1. Unprovisioned, the device opens an AP **csi-station** (password
   12345678); connect and open 192.168.4.1, fill in your WiFi SSID/pass
   in the form (or `POST /api/wifi`);
2. Once associated, CSI capture and the 1 Hz gateway ping start
   automatically and the display shows channel and waveform;
3. The same web page supports **dual-slot OTA upgrade** (upload .bin →
   write standby slot → verify, switch slot, reboot); use the STA IP once
   online.

## Display layout (128x64, five zones)

```
+----------------------------+
|ENV+CSI              -58 NOAP|  y0-7   title + RSSI/not associated
+----------------------------+  y9
|25.4°C            45.2%      |  y11-26 temp/hum (scale2, --.- on error)
+----------------------------+  y28-36 CH6 BW20 12PK/S / AP 192.168.4.1
+----------------------------+  y38
|  CSI amplitude waveform    |  y40-63 not assoc = CSI WAIT ASSOC
|            (24px tall)     |         n<2 = NO CSI PKTS
+----------------------------+
```

## SELFTEST sample (three lines, serialtap-aggregatable)

```
SELFTEST: board=esp32-c3-mini-env-csi-station fw=v0.1.0 chip=esp32c3 rev=v0.3 cores=1 flash=4MB psram=none sensor=AHT20 heap=140KB uid=xxxxxxxxxxxx
SELFTEST: i2c aht20=ok ssd1306=ok
SELFTEST: wifi aps=14 top="-41 ch6 MYSSID; -55 ch1 OTHER; ..." ours="MYSSID=-41"
```

## Firmware baseline compliance

| Item | Status |
|------|--------|
| Watchdog | ✅ TWDT 5 s panic (main loop + CSI stats task subscribed and fed) |
| Web/API OTA | ✅ dual OTA slots + streaming `POST /ota` |
| SELFTEST | ✅ three lines (identity / I2C sensors / WiFi scan) |

---
2026-10-22 · v0.1.0 initial release
