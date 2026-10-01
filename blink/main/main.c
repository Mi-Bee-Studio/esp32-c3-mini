/*
 * blink — ESP32-S3-Mini（Lolin S3 Mini）基线工程（测试固件）。
 *
 * 板载 WS2812 RGB（GPIO47）每秒步进一种颜色；每 10 秒一条心跳日志
 * （uptime/heap），供 serialtap 持续采集验证。同构拷贝自
 * esp32-s3-zero/blink（共性先拷贝规范）：颜色沿用本工作区状态语义
 * 绿=正常、琥珀=注意、蓝=跟踪中、暗=空闲。本板 BOOT 键 GPIO 未在
 * 官方页文档化，故不做按键交互（纯自动轮换）。
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "led_strip.h"
#include "app_web.h"

static const char *TAG = "BLINK";

#define WS2812_GPIO GPIO_NUM_47

typedef struct { uint8_t r, g, b; const char *name; } color_t;
static const color_t PALETTE[] = {
    {   0, 255,   0, "green"   },
    { 255, 140,   0, "amber"   },
    {   0, 120, 255, "blue"    },
    {  40,  40,  40, "dim"     },
};
#define PALETTE_N (sizeof(PALETTE) / sizeof(PALETTE[0]))

static led_strip_handle_t s_led;
static int s_color_idx;

static void led_set(int idx)
{
    const color_t *c = &PALETTE[idx % PALETTE_N];
    if (led_strip_set_pixel(s_led, 0, c->r, c->g, c->b) != ESP_OK ||
        led_strip_refresh(s_led) != ESP_OK) {
        ESP_LOGW(TAG, "WS2812 刷新失败");
        return;
    }
    ESP_LOGI(TAG, "LED %s", c->name);
}

void app_main(void)
{
    led_strip_config_t strip = {
        .strip_gpio_num = WS2812_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip, &rmt, &s_led));
    led_strip_clear(s_led);

    ESP_LOGI(TAG, "blink ready: ws2812=GPIO%d heap=%uK psram=%uK",
             WS2812_GPIO,
             (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    /* 板端维护页 :80（WiFi 配网 / OTA 刷机 / 状态），自带 APSTA 热点兜底 */
    app_web_init();

    /* 主循环看门狗：1s 一拍喂狗，卡死 >5s 触发 panic 重启自恢复 */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t werr = esp_task_wdt_init(&wdt_cfg);
    if (werr != ESP_OK && werr != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(werr);
    }
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    int beat = 0;
    while (true) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_color_idx++;
        led_set(s_color_idx);
        if (++beat >= 10) {
            beat = 0;
            ESP_LOGI(TAG, "heartbeat uptime=%llds heap=%uK",
                     (long long)(esp_timer_get_time() / 1000000),
                     (unsigned)(esp_get_free_heap_size() / 1024));
        }
    }
}
