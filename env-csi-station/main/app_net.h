#pragma once

/* 板端 Web 维护页 + WiFi 配网（app_net.c）：:80 —— 状态 / WiFi 配网 /
 * OTA 刷机 / 重启，并承担 WiFi 生命周期（APSTA + 凭据 NVS，
 * 命名空间 "wifi"，键 ssid/pass），事件联动 app_csi（关联开 CSI/断开停 ping）。
 * 同构拷贝自本仓 blink 工程的 app_web 模块（web 配网/OTA 家族基线），
 * 改动点见 app_net.c 文件头注释。未配网时热点 "csi-station"（密码
 * 12345678）→ 192.168.4.1；在网用 STA IP 访问。 */
void app_net_init(void); /* WiFi APSTA + NVS 凭据 + Web 维护页(:80) + 事件→app_csi_on_wifi */
