#pragma once

/* 板端 Web 维护页 + WiFi 配网（app_net.c）：:80 —— 状态 / WiFi 配网 /
 * OTA 刷机 / 重启，并承担 WiFi 生命周期（APSTA + 凭据 NVS，
 * 命名空间 "wifi"，键 ssid/pass），事件联动 app_csi（关联开 CSI/断开停 ping）。
 * 同构拷贝自本仓 blink 工程的 app_web 模块（web 配网/OTA 家族基线），
 * 改动点见 app_net.c 文件头注释。未配网时热点 "csi-station"（密码
 * 12345678）→ 192.168.4.1；在网用 STA IP 访问。 */
#include <stdbool.h>

void app_net_init(void); /* WiFi APSTA + NVS 凭据 + Web 维护页(:80) + 事件→app_csi_on_wifi */

/* 自检扫描占用 STA（main.c scan_task 专用，必须成对调用）。
 * hold=true ：置占用标志并 esp_wifi_disconnect()——中止进行中的关联尝试，
 *             STA 回到空闲；占用期间断线事件处理器让路、不抢重连
 *             （否则 1s 后的重连会打断扫描）。
 * hold=false：清标志；有凭据则 esp_wifi_connect() 恢复关联流程。
 * 背景：STA "正在关联"时驱动拒绝扫描（ESP_ERR_WIFI_STATE），见 main.c
 * scan_task 头注释（2026-11-11 aps=0 假象）。 */
void app_net_scan_hold(bool hold);
