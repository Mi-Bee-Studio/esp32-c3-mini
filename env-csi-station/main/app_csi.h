#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t pkts;      /* 累计 CSI 包 */
    float    rate_hz;   /* 每秒包率 */
    int8_t   rssi;      /* 最近包 RSSI，无包=-128 */
    uint8_t  nsc;       /* 最近包子载波数 */
    float    amp;       /* EMA 平均幅度 */
} csi_status_t;
void app_csi_init(void);
void app_csi_on_wifi(bool associated); /* STA 关联成功→使能 CSI+网关 ping；断开→停 ping */
void app_csi_get_status(csi_status_t *out);
int  app_csi_waveform(float wave[128]); /* 返回样本数，wave[0]=最旧 */
