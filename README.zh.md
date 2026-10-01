# ESP32-S3-Mini（Lolin S3 Mini 主板）

[English](README.md) | [中文文档](README.zh.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/esp32-s3-mini/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/esp32-s3-mini/actions/workflows/build.yml)

主板目录规范的一块板。**本仓库按"主板为根"规范组织**：

```
esp32-s3-mini/
├── README.md          # 本文件：这块板的一切硬件信息
└── <project>/         # 每个用这块板做的项目一个目录（按能力命名）
    ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
    └── README.md      # 项目说明 + 编译/烧录命令
```

规范要点：

- **主板目录名** = 板子名（kebab-case），根 README 只写硬件、不写业务；
- **项目目录独立可编译**：自带完整构建三件套，`cd <project> && idf.py build` 即出固件；
- 项目间不共享代码；需要共性时先拷贝，稳定后再考虑抽组件。

### 固件基线规范（全家桶强制）

1. **看门狗：必须启用**。任务订阅 ESP-IDF TWDT 按周期喂狗；
2. **Web/API 固件升级（OTA）：硬件允许则必须提供**。本板有 WiFi、4MB flash 放得下 OTA 双槽，每个项目都要带板端 web 刷机能力。

| 项目 | 看门狗 | Web/API OTA |
|------|--------|-------------|
| blink | ✅ 任务订阅 TWDT（5s panic） | ✅ OTA 双槽 + `POST /ota` 流式写槽 |

---

## 板子概要

| 项目 | 值 |
|------|-----|
| 模组 | **ESP32-S3FH4R2** —— Xtensa LX7 双核 240MHz（片内封装 4MB flash + 2MB Octal PSRAM） |
| Flash | 4MB（模组内嵌） |
| PSRAM | 2MB Octal（模组内嵌；**本仓未实测**——启用 `SPIRAM` 前先真机验证，教训见 `esp32-s3-zero` 根 README） |
| 无线 | 2.4GHz WiFi b/g/n + BLE 5 |
| USB | 原生 USB Type-C（USB OTG / **USB-Serial-JTAG**：控制台/烧录同一口，无桥芯片） |
| 板载 LED | **WS2812 RGB = GPIO47**（可寻址） |
| 按键 | RESET + BOOT（**BOOT 的 GPIO 未在官方页文档化**——进下载模式按住 BOOT 插 USB） |
| 尺寸 | 34.3 × 25.4 mm |
| 出处 | 板级事实取自 [Wemos/Lolin 官方页](https://www.wemos.cc/en/latest/s3/s3_mini.html)——"esp32-s3-mini" 名下最常见即此板；**若你的载板是别家，以实板为准** |

## 引脚位置图（USB-C 朝上，正面/元件面视角；功能引脚图）

```
                 ┌─ USB-C ─┐
                 │ [WS2812]│
                 │  =IO47  │   ESP32-S3FH4R2
                 │ [RST]   │   4MB flash + 2MB Octal PSRAM（片内）
                 │ [BOOT]  │   27× IO 引出（两侧排针）
                 └─────────┘
  本板物理排布未在官方页以表格形式文档化——引脚名称/排布以
  wemos.cc 官方 pinout 图为准（勿按本文臆造接线）。
```

要点：

- GPIO33–37 被 Octal PSRAM 占用（S3 通用约束）；
- GPIO19/20 = USB D-/D+，勿挪用；GPIO0 = BOOT（上电电平决定启动模式，官方页未明示，接入前实测）；
- WS2812 在 GPIO47（本仓 blink 使用中）。

## 注意事项

- **无 USB-UART 桥**：串口即 S3 的 USB-Serial-JTAG；
- **PSRAM 未实测**：S3FH4R2 标称 2MB Octal PSRAM，但同家族的 esp32-s3-zero 有 PSRAM 初始化挂死前科——本仓固件先 `CONFIG_SPIRAM=n`，启用前真机验证；
- BOOT 键 GPIO 官方页未文档化——进下载模式：按住 BOOT 插 USB / 按 RESET。

## 项目索引

| 项目 | 说明 |
|------|------|
| [blink](blink/README.zh.md) | 基线工程/测试固件：WS2812（GPIO47）状态色轮换 + web 维护页（配网/OTA）+ TWDT 看门狗 |
