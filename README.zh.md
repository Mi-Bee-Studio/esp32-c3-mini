# ESP32-C3-Mini（Lolin C3 Mini 主板）

[English](README.md) | [中文文档](README.zh.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/esp32-c3-mini/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/esp32-c3-mini/actions/workflows/build.yml)

主板目录规范的一块板。**本仓库按"主板为根"规范组织**：

```
esp32-c3-mini/
├── README.md          # 本文件：这块板的一切硬件信息
└── <project>/         # 每个用这块板做的项目一个目录（按能力命名）
    ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
    └── README.md      # 项目说明 + 编译/烧录命令
```

规范要点：

- **主板目录名** = 板子名（kebab-case），根 README 只写硬件、不写业务；
- **项目目录独立可编译**：自带完整 ESP-IDF 工程三件套（顶层 CMakeLists、
  `main/`、`sdkconfig.defaults`），`cd <project> && idf.py build` 即出固件；
- 项目间不共享代码；需要共性时先拷贝，稳定后再考虑抽组件。

### 固件基线规范（全家桶强制）

1. **看门狗：必须启用**。任务订阅 ESP-IDF TWDT 按周期喂狗；
2. **Web/API 固件升级（OTA）：硬件允许则必须提供**。本板有 WiFi、4MB flash
   放得下 OTA 双槽，故每个项目都要带板端 web 刷机能力。

| 项目 | 看门狗 | Web/API OTA |
|------|--------|-------------|
| blink | ✅ 任务订阅 TWDT（5s panic） | ✅ OTA 双槽 + `POST /ota` 流式写槽 |

---

## 板子概要

| 项目 | 值 |
|------|-----|
| 模组/芯片 | **ESP32-C3FH4** —— RISC-V 单核 160MHz（4MB flash 片内封装，无 PSRAM） |
| Flash | 4MB（芯片内封装） |
| PSRAM | 无 |
| 无线 | 2.4GHz WiFi b/g/n + Bluetooth 5 (LE) |
| USB | 原生 USB Type-C（**USB-Serial-JTAG**：控制台/烧录同一口，无桥芯片） |
| 板载 LED | **WS2812B RGB = GPIO7**（可寻址；Arduino 官方板级定义 `PIN_RGB_LED=7`） |
| 按键 | BOOT = GPIO9（C3 标配 strapping，按住上电进下载）、RESET |
| 引出 | 12× 数字 IO（D1 mini 盾兼容形制） |
| 尺寸 | 34.3 × 25.4 mm（D1 mini 形制，可插 Lolin/D1 mini 盾） |
| 出厂 | 默认带 MicroPython；本仓用 **ESP-IDF**（v6.0） |
| 出处 | 板级事实取自 [Wemos/Lolin 官方页](https://www.wemos.cc/en/latest/c3/c3_mini.html) + [Arduino 官方板级定义](https://github.com/espressif/arduino-esp32/tree/master/variants/lolin_c3_mini)——"esp32-c3-mini" 名下最常见即此板；**若你的载板是别家，以实板为准** |

## 引脚位置图（USB-C 朝上，正面/元件面视角；功能引脚图）

```
                 ┌─ USB-C ─┐
                 │ [WS2812]│
                 │  =GPIO7 │   ESP32-C3FH4
                 │ [RST]   │   4MB flash（片内，无 PSRAM）
                 │ [BOOT]  │   12× IO（D1 mini 形制）
                 └─────────┘
  物理排布未在官方页以表格形式文档化——引脚名称/排布以
  wemos.cc 官方 pinout 图为准（勿按本文臆造接线）。
```

要点（引脚功能出处：Arduino 官方板级定义 `lolin_c3_mini/pins_arduino.h`）：

- **WS2812 RGB = GPIO7**；**BOOT = GPIO9**；UART0 TX=GPIO21 · RX=GPIO20；
- I2C：SDA=GPIO8 · SCL=GPIO10；SPI：SS=GPIO5 · MOSI=GPIO4 · MISO=GPIO3 · SCK=GPIO2；
- ADC：A0–A5 = GPIO0–5；
- **GPIO18/19 = USB D-/D+**（C3 通用约束），勿挪用；GPIO2/8/9 为 strapping 脚，接外设注意上电电平。

## 注意事项

- **无 USB-UART 桥**：串口即 C3 的 USB-Serial-JTAG（控制台/烧录/JTAG 同口）；
- 进下载模式：按住 BOOT（GPIO9）插 USB / 按 RESET；
- 无 PSRAM——大缓冲方案按 400KB 级 SRAM 规划（同 luatos-esp32c3 量级）；
- 出厂带 MicroPython——首次刷 ESP-IDF 固件会覆盖它（可刷回）；
- D1 mini 盾兼容是这块板的差异化卖点（3.3V 逻辑，注意与老 5V 盾的兼容性）。

## 项目索引

| 项目 | 说明 |
|------|------|
| [blink](blink/README.zh.md) | 基线工程/测试固件：WS2812（GPIO7）状态色轮换 + BOOT 交互 + web 维护页（配网/OTA）+ TWDT 看门狗 |
