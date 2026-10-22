/*
 * app_csi — WiFi CSI 采集（轻量统计 + 环形波形版）。
 * 参考同构来源 luatos-esp32c3/air101-lcd/wifi-csi-sensing/main/app_sense.c：
 *   - wifi_csi_config_t 逐项一致（lltf/htltf/ltf_merge=true、
 *     channel_filter=false、manu_scale=false、dump_ack_en=true），保持默认
 *     modem PS——勿 WIFI_PS_NONE（繁忙信道帧处理吃满单核、饿死 IDLE →
 *     task_wdt 刷屏，csi-sensing 真机教训）；
 *   - ACK 激励原理：向网关联 1Hz ping（esp_ping_new_session，count=0 无限、
 *     data_size=8），TX 后无线电台清醒，网关回包与 ACK 帧即刻可达，
 *     dump_ack_en 使 ACK 帧也携带 CSI → 低流量环境也有稳定采样。
 *     1Hz 取保守值（桌面站安静采）；10Hz 版本在 csi-sensing。
 *   - 回调跑 WiFi 任务上下文：只做全子载波幅度计算 + 临界区写环形缓冲，
 *     绝不阻塞/打印（同 app_sense.c 回调纪律）。
 */
#include "app_csi.h"

#include <math.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

static const char *TAG = "csi";

#define CSI_RING_N      128
#define STATS_TASK_STACK 3072
#define STATS_TASK_PRIO  3

/* 回调写、stats 任务读；统计快照由 stats 任务写、任意读者读 */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static float s_ring[CSI_RING_N]; /* 幅度环形缓冲，wave[0]=最旧 */
static int s_head;               /* 下一写入位（模 CSI_RING_N） */
static int s_count;              /* 环内样本数（≤CSI_RING_N） */
static float s_rate_hz;          /* 每秒包率（stats 任务 1s 一算） */
static float s_amp_ema;          /* 幅度 EMA（新样本权重 0.2） */
static bool s_amp_valid;

static volatile uint32_t s_pkts;
static volatile int8_t s_rssi = -128; /* 无包=-128 */
static volatile uint8_t s_nsc;

static bool s_csi_on;                /* 使能幂等标志 */
static esp_ping_handle_t s_ping;     /* 网关 ping 激励会话 */

/* ---------------------------------------------------------------------------
 * CSI 接收回调（WiFi 任务上下文：无日志/无阻塞，临界区内只写数组）
 * ------------------------------------------------------------------------- */
static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;
    const int nsc = info->len / 2;
    if (nsc < 16) {
        return;
    }
    /* 全子载波平均幅度：buf 为 signed-int8 I/Q 交错对 → hypotf 求模累加 */
    float sum = 0.0f;
    for (int k = 0; k < nsc; k++) {
        const float i = (int8_t)info->buf[2 * k];
        const float q = (int8_t)info->buf[2 * k + 1];
        sum += hypotf(i, q);
    }
    const float amp = sum / (float)nsc;

    portENTER_CRITICAL(&s_mux);
    s_ring[s_head] = amp;
    s_head = (s_head + 1) % CSI_RING_N;
    if (s_count < CSI_RING_N) {
        s_count++;
    }
    portEXIT_CRITICAL(&s_mux);

    s_pkts++;
    s_rssi = info->rx_ctrl.rssi;
    s_nsc = (uint8_t)nsc;
}

/* ---------------------------------------------------------------------------
 * CSI 使能（WiFi 关联后调用，幂等）——配置逐项同 app_sense.c
 * ------------------------------------------------------------------------- */
static void csi_enable(void)
{
    if (s_csi_on) {
        return;
    }
    /* 勿 WIFI_PS_NONE：保持默认 modem PS（MIN_MODEM），采样率靠 ping 激励 */
    wifi_csi_config_t cfg = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = false, /* 保持子载波独立性 */
        .manu_scale = false,
        .dump_ack_en = true,        /* ACK 激励：ACK 帧也携带 CSI */
    };
    esp_err_t ret = esp_wifi_set_csi_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CSI 配置失败: %s（sdkconfig 需 CONFIG_ESP_WIFI_CSI_ENABLED）",
                 esp_err_to_name(ret));
        return;
    }
    ret = esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_csi(true);
    }
    if (ret == ESP_OK) {
        s_csi_on = true;
        ESP_LOGI(TAG, "CSI 采集已使能");
    } else {
        ESP_LOGW(TAG, "CSI 使能失败: %s", esp_err_to_name(ret));
    }
}

/* ---------------------------------------------------------------------------
 * 网关 ping 激励（1Hz）：回包=下行单播数据帧，ACK 帧随行带出 CSI
 * ------------------------------------------------------------------------- */
static void ping_start(void)
{
    if (s_ping) {
        return;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.gw.addr == 0) {
        return;
    }
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 0;          /* 无限 */
    cfg.interval_ms = 1000; /* 1Hz 保守值（10Hz 版本在 csi-sensing） */
    cfg.timeout_ms = 1000;
    cfg.data_size = 8;      /* 小包即可 */
    char gws[18];
    snprintf(gws, sizeof(gws), IPSTR, IP2STR(&ip.gw));
    if (ipaddr_aton(gws, &cfg.target_addr) == 0) {
        return;
    }
    esp_ping_callbacks_t cbs = { 0 };
    if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK) {
        esp_ping_start(s_ping);
        ESP_LOGI(TAG, "网关 ping 激励 %s @1Hz（回包/ACK 帧出 CSI）", gws);
    } else {
        s_ping = NULL;
        ESP_LOGW(TAG, "ping 会话创建失败");
    }
}

static void ping_stop(void)
{
    if (s_ping) {
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);
        s_ping = NULL;
    }
}

/* ---------------------------------------------------------------------------
 * 每秒统计任务：包率 = pkts 增量；幅度 EMA（新样本 0.2 权重）
 * ------------------------------------------------------------------------- */
static void stats_task(void *arg)
{
    (void)arg;
    const esp_err_t werr = esp_task_wdt_add(NULL);
    if (werr != ESP_OK && werr != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(werr);
    }
    uint32_t last_pkts = 0;
    bool ema_valid = false;
    float ema = 0.0f;
    while (true) {
        esp_task_wdt_reset();
        const uint32_t pkts = s_pkts;
        const float rate = (float)(pkts - last_pkts);
        last_pkts = pkts;

        portENTER_CRITICAL(&s_mux);
        const int n = s_count;
        float last_amp = 0.0f;
        if (n > 0) {
            const int idx = (s_head - 1 + CSI_RING_N) % CSI_RING_N;
            last_amp = s_ring[idx];
        }
        portEXIT_CRITICAL(&s_mux);

        if (n > 0) {
            ema = ema_valid ? (ema * 0.8f + last_amp * 0.2f) : last_amp;
            ema_valid = true;
        }
        portENTER_CRITICAL(&s_mux);
        s_rate_hz = rate;
        s_amp_ema = ema_valid ? ema : 0.0f;
        s_amp_valid = ema_valid;
        portEXIT_CRITICAL(&s_mux);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---------------------------------------------------------------------------
 * 对外 API
 * ------------------------------------------------------------------------- */
void app_csi_on_wifi(bool associated)
{
    if (associated) {
        csi_enable(); /* 幂等：重复关联不重复使能 */
        ping_start();
    } else {
        ping_stop(); /* CSI 保持使能无妨：无关联即无包 */
    }
}

void app_csi_get_status(csi_status_t *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    out->pkts = s_pkts;
    out->rate_hz = s_rate_hz;
    out->amp = s_amp_valid ? s_amp_ema : 0.0f;
    portEXIT_CRITICAL(&s_mux);
    out->rssi = s_rssi;
    out->nsc = s_nsc;
}

int app_csi_waveform(float wave[128])
{
    portENTER_CRITICAL(&s_mux);
    const int n = s_count;
    int idx = s_head - s_count; /* 未满环时最旧=0；满环时最旧=head */
    if (idx < 0) {
        idx += CSI_RING_N;
    }
    for (int k = 0; k < n; k++) {
        wave[k] = s_ring[idx];
        idx = (idx + 1) % CSI_RING_N;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

void app_csi_init(void)
{
    xTaskCreate(stats_task, "csi_stats", STATS_TASK_STACK, NULL,
                STATS_TASK_PRIO, NULL);
}
