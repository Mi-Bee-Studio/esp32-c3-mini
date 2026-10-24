# env-csi-station — AHT20 温湿度 + WiFi CSI 采集站

[English](README.md) | [中文文档](README.zh.md)

ESP32-C3 SuperMini（nologo，C3FN4）上的桌面采集站：**AHT20 温湿度**（硬 I2C0）
+ **HC-SR501 人体感应**（OUT→GPIO4，数字推挽输出：ANYEDGE 中断 + 3s 去抖计数、
120s 在场窗、上电 60s 预热期间忽略事件，见 main/app_pir.c）+ **WiFi CSI 信道状态
采集**（关联后 1Hz 网关 ping 激励，ACK 帧携带 CSI 出样本，原理见 main/app_csi.c
头注释）+ **SSD1306 128×64 屏显**（已拆走单独调试，固件仍开机自扫其回归位硬
I2C0），带 Web 配网与 OTA 双槽升级。PIR 状态上报：`/api/status`
（`pir`/`pir_cnt`/`pir_last`/`present`）、Web 状态卡（人在/无人）、心跳行、
SELFTEST。同构来源：blink（web 配网/OTA 基线）、esp32-s2-mini/env-station
（AHT20 驱动）、esp32-s3-zero/env-station（SSD1306 驱动）、
luatos-esp32c3/air101-lcd/wifi-csi-sensing（CSI 配置与 ping 激励）。

## 接线

| 外设 | 引脚 | 说明 |
|------|------|------|
| AHT20 SDA | **GPIO8**（I2C0 SDA） | GPIO8 同板载蓝灯（低电平点亮 + 10k 上拉）：I2C 流量会让蓝灯微闪属正常，勿外加强下拉；亦为 strapping 脚，复位时勿拉低 |
| AHT20 SCL | **GPIO9**（I2C0 SCL） | GPIO9=BOOT 键：运行中按住会毁一次 I2C 读（固件自动重试），勿常按 |
| AHT20 VCC/GND | 3V3 / G | 3.3V 供电 |
| HC-SR501 OUT | **GPIO4**（数字 4 焊盘） | 数字推挽输出（有人=高）；模块自带可调重触发/保持时间电位器。固件配内部下拉（线脱落时电平仍定义）；ANYEDGE 中断、上升沿 ≥3s 间距才计数（去抖）、120s 在场窗、上电 60s 预热期间忽略事件 |
| HC-SR501 VCC/GND | 3V3 / G | 3.3V 供电 |
| SSD1306 SDA/SCL | **GPIO8/9（与 AHT20 共线硬 I2C0）——屏已拆走单独调试** | 屏当前不在板上；回归位 = 板丝印 SDA/SCL = GPIO8/9，开机自扫 {0x3C,0x3D}。原软 I2C(4/6) 扫描已收敛移除——GPIO4 现归 PIR |
| SSD1306 VCC/GND | 3V3 / G | 地址 **0x3C** 或 0x3D（SA0=1）；模块需上拉（多数 4 脚模块板载 4.7-10k） |

**开机自适应**：固件扫硬 I2C0（8/9）× 双地址 {0x3C,0x3D}——屏的回归位——首个
ACK 即驱动；落点报在 `SELFTEST: i2c ... ssd1306=hw:0x3C / err`，全 NACK 则无屏
模式继续跑（其余功能不受影响，屏拆走期间 err 属正常）。

**soft_i2c.c 为何仍编译**：C3 只有一路硬件 I2C 控制器（AHT20 与屏回归位共用）；
位带软 I2C 写路径留给未来需要第二条总线的外设。GPIO4 归 PIR 期间开机不使用
（GPIO6 空闲）。

## 构建（PowerShell 激活 IDF v6.0）

```powershell
# 激活本机 IDF v6.0 环境（按实际安装路径）
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
$env:IDF_PATH='C:\Users\micke\esp\v6.0\esp-idf'
$env:IDF_TOOLS_PATH='C:\Espressif\tools'
. C:\Users\micke\esp\v6.0\esp-idf\export.ps1
Set-Location D:\Projects\embedded\esp32-c3-mini\env-csi-station
idf.py set-target esp32c3
idf.py build
```

控制台/烧录走 **ROM USB-CDC**（本板无 USB-Serial-JTAG 外设，
sdkconfig.defaults 已设 CONFIG_ESP_CONSOLE_USB_CDC=y）。serialtap 代理烧录：

```bash
serialtap flash <设备> \
  <abs>bootloader.bin@0x0 <abs>partition-table.bin@0x8000 \
  <abs>ota_data_initial.bin@0x10000 <abs>env_csi_station.bin@0x20000 \
  --esptool C:/Espressif/tools/python/v6.0/venv/Scripts/esptool.exe \
  --chip esp32c3 --baud 460800    # bin 一律绝对路径
```

## 配网与 OTA

1. 未配网时设备开热点 **csi-station**（密码 12345678），手机/电脑连上后访问
   192.168.4.1，在表单里填家里 WiFi 的 SSID/密码保存（或 `POST /api/wifi`）；
2. 关联成功自动开 CSI + 1Hz 网关 ping 激励，屏显出现信道与波形；
3. Web 页支持 **OTA 双槽升级**（上传 .bin → 写备用槽 → 校验切槽重启），
   在网后用 STA IP 访问同一页面。

## 屏显布局（128×64，五区）

```
+----------------------------+
|ENV+CSI              -58 NOAP|  y0-7   标题 + RSSI/未关联
+----------------------------+  y9
|25.4°C            45.2%      |  y11-26 温·湿（scale2，读失败 --.-）
+----------------------------+  y28-36 CH6 BW20 12PK/S / AP 192.168.4.1
+----------------------------+  y38
| CSI 幅度波形（24px 高）      |  y40-63 未关联=CSI WAIT ASSOC
|                            |         n<2=NO CSI PKTS
+----------------------------+
```

## SELFTEST 样例（三行，serialtap 可聚合）

```
SELFTEST: board=esp32-c3-mini-env-csi-station fw=v0.1.0 chip=esp32c3 rev=v0.3 cores=1 flash=4MB psram=none sensor=AHT20+SR501 heap=140KB uid=xxxxxxxxxxxx
SELFTEST: i2c aht20=ok ssd1306=hw:0x3C pir=gpio4    # ssd1306：hw:0x3C / err（屏拆走期间 err 属正常）；PIR 为 GPIO 器件（开机 60s 预热）
SELFTEST: wifi aps=14 top="-41 ch6 MYSSID; -55 ch1 OTHER; ..." ours="MYSSID=-41"
```

## 固件基线合规

| 项 | 状态 |
|----|------|
| 看门狗 | ✅ TWDT 5s panic（主循环 + CSI 统计任务订阅喂狗） |
| Web/API OTA | ✅ 双 OTA 槽 + `POST /ota` 流式写槽 |
| SELFTEST | ✅ 三行（身份 / I2C 传感器 / WiFi 扫描） |

---
2026-10-22 · v0.1.0 初版
