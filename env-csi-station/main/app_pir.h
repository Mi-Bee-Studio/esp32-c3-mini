#pragma once
#include <stdbool.h>
#include <stdint.h>

/* HC-SR501 人体感应模块（OUT→GPIO4，数字推挽输出，检测到人输出高）。
 * 计数/在场语义（实现见 app_pir.c）：
 *   - ANYEDGE 中断 + ≥3s 上升沿间距去抖（SR501 触发沿抖动）；
 *   - 开机 60s 预热：期间事件不计数、不置在场（SR501 上电自校准期输出
 *     不可信），到点打一行 "PIR: 预热完成，开始计数"；
 *   - 在场窗 120s：level==1 或最近一次接受事件距今 <120s = 有人在。 */
#define PIR_GPIO_NUM         4    /* OUT 接数字 4 焊盘 */
#define PIR_DEBOUNCE_S       3    /* 上升沿最小间距（去抖） */
#define PIR_WARMUP_S         60   /* 开机预热：忽略事件 */
#define PIR_PRESENT_WINDOW_S 120  /* 在场窗：最近事件 <120s 视为在场 */

typedef struct {
    int level;       /* 当前 GPIO 电平 0/1 */
    uint32_t count;  /* 接受（去抖后）的运动事件数（预热期不计数） */
    int last_trig_s; /* 距最近一次接受事件秒数；-1=从未 */
    bool present;    /* level==1 || last_trig_s<PIR_PRESENT_WINDOW_S（预热期恒 0） */
} pir_status_t;

void app_pir_init(void);                   /* GPIO4+中断+预热定时器（开机调一次） */
void app_pir_get_status(pir_status_t *out); /* 状态快照（原子友好字段） */
void app_pir_poll(void);                   /* 主循环 1s 一拍：≥3s 节流打点运动事件（ISR 不打日志） */
