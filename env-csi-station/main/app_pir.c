/* app_pir — HC-SR501 人体感应（OUT→GPIO4）。
 * SR501：数字推挽输出（有人=高），自带可调重触发/保持时间电位器，上电后
 * ~60s 预热期输出不可信。固件对策：
 *   1. GPIO4 输入 + 内部下拉：线脱落时电平仍定义（SR501 推挽输出正常覆盖）；
 *   2. ANYEDGE 单 handler：每沿按 gpio_get_level 刷新电平，仅上升沿且距上
 *      次接受沿 ≥3s 才算一次事件（去抖 SR501 触发沿抖动）；
 *   3. ISR 只做原子友好更新（计数/时间戳/电平），无日志/浮点/malloc；
 *      日志在主循环消费者 app_pir_poll（≥3s 节流）；
 *   4. 开机 60s 预热：ISR 不计数不置在场；esp_timer 一次性到点打完成日志；
 *   5. 时间戳存"开机以来秒数"（32bit 单读原子，避免 64bit 撕裂读）。
 * 中断服务按非 IRAM 档安装（flag 0）：OTA 写 flash 屏蔽窗口内事件丢弃
 * （可接受），换取 handler 不必全 IRAM 化。 */
#include "app_pir.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "PIR";

#define PIR_DEBOUNCE_US (PIR_DEBOUNCE_S * 1000000LL)
#define PIR_WARMUP_US   (PIR_WARMUP_S * 1000000LL)

/* --- ISR 侧（volatile；s_cnt/s_last_trig_s/s_level 单 32bit/int 读原子） --- */
static volatile int s_level;            /* 最近电平（每沿刷新） */
static volatile uint32_t s_cnt;         /* 接受事件数（预热后） */
static volatile uint32_t s_last_trig_s; /* 最近接受事件的 uptime 秒 */
static volatile bool s_have_trig;       /* 是否有过接受事件 */
static volatile bool s_warmup_done;     /* 60s 预热完成 */
static int64_t s_last_edge_us;          /* 去抖基准（仅 ISR 读写） */
static int s_prev_lvl;                  /* 上一沿电平（仅 ISR 读写） */

static void pir_isr(void *arg)
{
    (void)arg;
    const int lvl = gpio_get_level((gpio_num_t)PIR_GPIO_NUM);
    const int64_t now = esp_timer_get_time();
    s_level = lvl;
    /* 仅上升沿（0→1）且 ≥3s 间距才算一次事件（去抖） */
    if (lvl && !s_prev_lvl && now - s_last_edge_us >= PIR_DEBOUNCE_US) {
        s_last_edge_us = now;
        if (s_warmup_done) {
            s_cnt++;
            s_last_trig_s = (uint32_t)(now / 1000000);
            s_have_trig = true;
        }
    }
    s_prev_lvl = lvl;
}

static void warmup_done_cb(void *arg)
{
    (void)arg;
    s_warmup_done = true;
    ESP_LOGI(TAG, "预热完成，开始计数");
}

void app_pir_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIR_GPIO_NUM,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, /* 线脱落时电平仍定义 */
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    /* ISR 服务只装一次（已装返回 INVALID_STATE 容忍） */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)PIR_GPIO_NUM, pir_isr, NULL));

    s_level = gpio_get_level((gpio_num_t)PIR_GPIO_NUM);
    s_prev_lvl = s_level;

    const esp_timer_create_args_t tcfg = {
        .callback = warmup_done_cb,
        .name = "pir_warmup",
    };
    esp_timer_handle_t t;
    ESP_ERROR_CHECK(esp_timer_create(&tcfg, &t));
    ESP_ERROR_CHECK(esp_timer_start_once(t, PIR_WARMUP_US));

    ESP_LOGI(TAG, "SR501 @GPIO%d 就绪（ANYEDGE 中断+3s 去抖；60s 预热忽略，"
                  "120s 在场窗；当前 level=%d%s）",
             PIR_GPIO_NUM, s_level,
             s_level ? "（预热期高电平属正常）" : "");
}

void app_pir_get_status(pir_status_t *out)
{
    if (!out) {
        return;
    }
    const uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);
    out->level = s_level;
    out->count = s_cnt;
    out->last_trig_s = s_have_trig ? (int32_t)(now_s - s_last_trig_s) : -1;
    out->present = s_warmup_done
                   && (s_level == 1
                       || (s_have_trig && out->last_trig_s < PIR_PRESENT_WINDOW_S));
}

void app_pir_poll(void)
{
    /* 主循环消费者：事件已由 ISR 去抖（≥3s 间距），此处再 ≥3s 节流打点；
     * 节流窗内的事件不丢——计数已含，下一拍补打（合并为一行）。 */
    static uint32_t s_logged_cnt;
    static uint32_t s_prev_ev_uptime;
    static bool s_have_prev;
    static int64_t s_last_log_us;

    pir_status_t p;
    app_pir_get_status(&p);
    if (p.count == s_logged_cnt) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_log_us < PIR_DEBOUNCE_US) {
        return; /* 节流窗内：留待下拍 */
    }
    const uint32_t ev_uptime =
        (uint32_t)(now_us / 1000000) - (uint32_t)p.last_trig_s;
    const int64_t gap_s =
        s_have_prev ? (int64_t)(ev_uptime - s_prev_ev_uptime) : -1;
    ESP_LOGI(TAG, "motion #%u level=%d last=%llds",
             (unsigned)p.count, p.level, gap_s);
    s_last_log_us = now_us;
    s_logged_cnt = p.count;
    s_prev_ev_uptime = ev_uptime;
    s_have_prev = true;
}
