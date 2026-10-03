# weather-clock — ST7735 TFT + AHT30 温湿度时钟

> 从用户 Arduino 工程 `esp32c3-pro-0.96-aht30`（同接线）移植到 ESP-IDF 的实机工程。

## 功能

- **ST7735 0.96" 160x80 mini TFT**（硬件 SPI，rotation 3 横屏）：每秒刷 NTP 时间
  （UTC+8，内网 NTP `192.168.63.1` + `pool.ntp.org` 兜底，未同步显示 `--:--:--`）；
- **AHT30 温湿度**（I2C 0x38）：每 5 秒读一次，随 5 秒轮换的**随机底色**
  （每通道 64..255）+ 自动黑/白对比文字，版式同 Arduino 原版（时间/温度/湿度三行 2x 字 + 底部日期 1x 字）；
- **板端 Web 维护页 :80**：WiFi 配网 / OTA 刷机 / 重启（未配网时救援热点
  `wclock-c3m` / `12345678` → 192.168.4.1）；
- **看门狗**：ESP-IDF TWDT，5s 超时 panic（主循环 1s 一拍喂狗）；
- **OTA**：双 OTA 槽（app 在 `0x20000`），Web 页流式写备用槽 → 校验 → 切槽 → 重启。

## 接线（沿用 Arduino 工程 `app_config.h`）

| 信号 | GPIO | 备注 |
|------|------|------|
| TFT SCLK | 4 | |
| TFT MOSI | 6 | |
| TFT CS   | 7 | 兼板载 WS2812 DIN——CS 翻转会令 LED 偶发乱闪，属预期 |
| TFT DC   | 5 | |
| TFT RST  | 3 | |
| TFT BL   | 10 | 高电平点亮 |
| AHT30 SDA | 8 | C3 strapping（上电需高），模块上拉满足 |
| AHT30 SCL | 9 | 兼 BOOT 键——按下会拉低 SCL 令当次读数失败，忽略即可 |

面板初始化复刻 Adafruit `initR(INITR_MINI160x80)` 序列（Rcmd1 + Rcmd2green160x80 +
Rcmd3 + RGB 滤色 MADCTL）；rotation 3 = `MADCTL 0x60`，画面偏移 X=0 / Y=24
（面板 RAM 132x162 取窗）。绘图走 `esp_lcd` panel_io 同步事务 + 160 像素行缓冲，
无整帧 framebuffer（C3 无 PSRAM）。

## 编译与烧录

```bash
cd weather-clock
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor   # 本板为原生 USB-Serial-JTAG
```

## 踩坑记录（复刻/移植时必读）

- **`esp_lcd` 的 `trans_queue_depth` 必须 ≥1**：该值直接成为 spi 设备事务队列
  长度，填 0（旧版"同步事务"语义）会在 `xQueueCreate(0,…)` 断言崩溃
  （queue.c:573）；
- **IDF v6 启动期已初始化 TWDT**：主循环再 `esp_task_wdt_init` 会报
  `already initialized`，应改用 `esp_task_wdt_reconfigure` 收编默认配置；
- **AHT30 重试要幂等**：`i2c_new_master_bus` 二次调用会撞
  `bus already acquired`；初始化成功后只重试 `read`；
- **RF 注意**：Lolin C3 Mini 的 PCB 天线在板边——面包板插满、杜邦线压线或
  金属桌面近场会显著劣化收发。实测本板插面包板时救援热点不可见、STA 认证
  超时（`auth -> init (0x200)`），同一位置 Arduino 固件同样连不上；
  屏显排布时给天线区留空。

## 固件基线

| 基线 | 状态 |
|------|------|
| 看门狗 | ✅ TWDT 5s panic，主循环喂狗 |
| Web/API OTA | ✅ OTA 双槽 + `POST /ota` 流式写槽 |
