# ESP32-C3-Mini（nologo ESP32-C3 SuperMini 主板）

[English](README.md) | [中文文档](README.zh.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/esp32-c3-mini/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/esp32-c3-mini/actions/workflows/build.yml)

<img src="https://www.nologo.tech/assets/img/esp32/esp32c3supermini/esp32c3.png" alt="ESP32-C3 SuperMini" width="420">

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
| weather-clock | ✅ 任务订阅 TWDT（5s panic） | ✅ OTA 双槽 + `POST /ota` 流式写槽 |
| env-csi-station | ✅ 任务订阅 TWDT（5s panic） | ✅ OTA 双槽 + `POST /ota` 流式写槽 |

---

## 板子概要

| 项目 | 值 |
|------|-----|
| 模组/芯片 | **ESP32-C3FN4** —— RISC-V 单核 @160MHz（4MB flash 封装内，无 PSRAM） |
| Flash | 4MB（封装内） |
| SRAM | 400KB（+ ROM 384KB） |
| PSRAM | 无 |
| 无线 | 2.4GHz WiFi b/g/n + Bluetooth 5 (LE) |
| USB | 原生 USB Type-C —— **ROM USB-CDC**（控制台 + 烧录；无桥片、无 USB-Serial-JTAG 外设） |
| 板载 LED | **蓝色 LED @GPIO8，低电平点亮**（VSYS→5.1k→LED→GPIO8，另有 10k 上拉；另有一颗电源红灯） |
| 按键 | BOOT = GPIO9（10k 上拉；按住 + RESET 进下载模式）、RESET |
| 供电 | USB 5V，或 5V 焊盘外部 3.3-6V（ME6211 LDO；**USB 与外部供电互斥**） |
| 天线 | 板载 PCB 天线（预留外接天线焊盘/座） |
| 深睡电流 | ~43µA |
| 尺寸 | 22.52 × 18mm，单面 SMT |
| 引出 | 11× GPIO（GPIO0-10）+ GPIO20/21（UART0）、4× ADC |
| 出处 | [nologo 产品页](https://www.nologo.tech/product/esp32)（引脚功能另见 [Foot/Pin 用法页](https://www.nologo.tech/esp32C3SuperMiniFoot)）；板型 2026-10-22 实物更正（此前误记为 Lolin C3 Mini） |

## 引脚位置图（USB-C 朝上，正面/元件面视角）

```
                 ┌── USB-C ──┐
   左列（上→下） │  ME6211   │ 右列（上→下）
        5V ─────┤5V      GPIO5├──── A5 / MISO
        G  ─────┤G       GPIO6├──── MOSI
       3V3 ─────┤3V3     GPIO7├──── SS
      GPIO4 ────┤A4/SCK  GPIO8├──── SDA  [蓝LED=8 低亮] strapping
      GPIO3 ────┤A3      GPIO9├──── SCL  [BOOT=9] strapping
      GPIO2 ────┤A2     GPIO10├────
      GPIO1 ────┤A1     GPIO20├──── U0RXD
      GPIO0 ────┤A0     GPIO21├──── U0TXD
                 │ ESP32-C3FN4 │   PCB 天线
                 └─ 22.52×18mm─┘   [RST] 独立复位键
```

要点：

- I2C 默认 SDA=**GPIO8** / SCL=**GPIO9**（与蓝灯/BOOT 复用，见注意事项）；
- SPI 默认 SCK=GPIO4 · MISO=GPIO5 · MOSI=GPIO6 · SS=GPIO7；
- ADC：A0–A5 = GPIO0–5；UART0：TX=GPIO21 · RX=GPIO20；
- strapping：**GPIO2/8/9**（另 GPIO5 兼 VDD_SPI 相关脚），接外设注意上电电平；
- **GPIO18/19 = USB D±**（未引出，勿挪用）；GPIO11-17 为封装内 flash 所用。

## 注意事项

- **控制台是 ROM USB-CDC**：sdkconfig 需 `CONFIG_ESP_CONSOLE_USB_CDC=y`
  （blink/weather-clock 现用 USB_SERIAL_JTAG 配置在本板无效——本仓已知问题，
  env-csi-station 已修正，旧项目本轮未动）；
- 下载模式：**按住 BOOT（GPIO9）+ 短按 RESET**；个别线缆/主机需重插重试；
- **USB 与外部供电互斥**（同时接可能倒灌，5V 焊盘供电时勿再插 USB）；
- **GPIO8 = 板载蓝灯**（低电平点亮 + 10k 上拉）：作 I2C SDA 时蓝灯随流量微闪
  （无害）；同时是 strapping 脚——外设勿在复位时强拉低；
- **GPIO9 = BOOT 键**：与 SCL 复用，运行中按 BOOT 会毁一次 I2C 读（读失败会
  重试），勿常按；
- 无 PSRAM——大缓冲方案按 ~400KB SRAM 预算规划；
- 出厂固件为 Arduino 生态 MicroPython 类，刷 IDF 会覆盖（可重刷）。

## 项目索引

| 项目 | 说明 |
|------|------|
| [blink](blink/README.zh.md) | 基线工程/测试固件：WS2812 状态色轮换 + BOOT 交互 + web 维护页（配网/OTA）+ TWDT 看门狗（WS2812@GPIO7 依旧 Lolin 文档——本板无该灯，待改） |
| [weather-clock](weather-clock/README.zh.md) | ST7735 0.96" TFT（160x80）+ AHT30 温湿度时钟：NTP 时间、随机底色轮换温湿度显示 + web 配网/OTA（I2C 8/9） |
| [env-csi-station](env-csi-station/README.zh.md) | AHT20 温湿度（硬 I2C 8/9）+ WiFi CSI 采集（1Hz 网关 ping ACK 激励）+ SSD1306 屏显（软 I2C 4/6）+ web 配网/OTA |
