# blink — 基线工程（测试固件）

> 同构拷贝自 [`esp32-s3-zero/blink`](../../esp32-s3-zero/blink)（共性先拷贝规范）。

## 功能

- 板载 WS2812 RGB（GPIO47）每秒步进一种状态色（绿/琥珀/蓝/暗）；
- 本板 BOOT 键 GPIO 官方未文档化，故无按键交互（纯自动轮换）；
- 每 10 秒一条心跳日志（`uptime` / `heap`），供 serialtap 持续采集验证；
- 板端 **Web 维护页 :80**：WiFi 配网 / 固件 OTA / 重启（未配网时兜底热点见源码 `AP_SSID` / `12345678` → `192.168.4.1`）；
- **看门狗**：ESP-IDF TWDT 5s 超时 panic（主循环 1s 一拍喂狗）——基线规范强制；
- **OTA**：OTA 双槽（app 起始 `0x20000`），Web 页流式写备用槽 → 校验 → 切槽 → 重启。

## 构建与烧录

```bash
cd blink
idf.py set-target <target>   # 见 sdkconfig.defaults
idf.py build
idf.py -p COMx flash monitor
```

控制台走原生 USB-Serial-JTAG（本板无 USB-UART 桥）；release 附 `flash_blink.sh/.bat`。

## 固件基线规范核对

| 基线 | 状态 |
|------|------|
| 看门狗 | ✅ TWDT 5s panic，主循环喂狗 |
| Web/API OTA | ✅ OTA 双槽 + `POST /ota` 流式写槽 |
