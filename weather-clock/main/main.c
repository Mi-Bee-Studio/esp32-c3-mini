/*
 * weather-clock — ESP32-C3-Mini（Lolin C3 Mini）温湿度时钟。
 *
 * 从用户 Arduino 工程 esp32c3-pro-0.96-aht30（同接线）移植到 ESP-IDF：
 * ST7735 0.96" 160x80 mini TFT（硬件 SPI，引脚沿用参考 app_config.h：
 * SCLK=4 MOSI=6 CS=7 DC=5 RST=3 BL=10）+ AHT30（I2C SDA=8 SCL=9，参考工程
 * 走 Arduino C3 默认 Wire 引脚）。每秒刷 NTP 时间、每 5 秒读温湿度并换
 * 随机底色（64..255/通道）+ 自动黑/白对比文字，与参考版式一致。
 * 注意：GPIO7 兼作板载 WS2812 DIN——TFT CS 翻转会令 LED 偶发乱闪，属预期；
 * GPIO9 兼 BOOT 键——按下会拉低 SCL 令当次温湿度读数失败，忽略即可。
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aht30.h"
#include "app_web.h"
#include "st7735.h"

static const char *TAG = "wclock";

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF

/* 版式（160x80 横屏）：时间/温度/湿度各一行 2x 字，日期一行 1x 字垫底 */
#define TIME_Y 8
#define TEMP_Y 28
#define HUM_Y  48
#define DATE_Y 68

static uint16_t s_bg = COLOR_BLACK;
static uint16_t s_fg = COLOR_WHITE;
static float s_temp = 0.0f;
static float s_hum = 0.0f;
static bool s_have_sensor;
static volatile bool s_time_ok;

static void draw_centered(int y, const char *s, int scale)
{
    st7735_draw_text((ST7735_W - st7735_text_width(s, scale)) / 2, y,
                     s, s_fg, s_bg, scale);
}

/* 参考工程 getRandomColor()：每通道 64..255，且与上次底色不同 */
static uint16_t random_bg(void)
{
    uint16_t c;
    do {
        uint32_t r = esp_random();
        uint8_t cr = 64 + (r >> 0) % 192;
        uint8_t cg = 64 + (r >> 8) % 192;
        uint8_t cb = 64 + (r >> 16) % 192;
        c = RGB565(cr, cg, cb);
    } while (c == s_bg);
    return c;
}

/* 参考工程 getContrastingColor()：按亮度选黑/白文字 */
static uint16_t contrast_for(uint16_t bg)
{
    unsigned r = ((bg >> 11) & 0x1F) * 8;
    unsigned g = ((bg >> 5) & 0x3F) * 4;
    unsigned b = (bg & 0x1F) * 8;
    unsigned lum = (r * 299 + g * 587 + b * 114) / 1000;
    return lum > 128 ? COLOR_BLACK : COLOR_WHITE;
}

/* 5 秒整屏重绘（同参考 updateTemperatureAndHumidity 的 fillScreen 流程） */
static void repaint(void)
{
    s_bg = random_bg();
    s_fg = contrast_for(s_bg);
    st7735_fill_screen(s_bg);

    char line[24];
    if (s_have_sensor) {
        snprintf(line, sizeof(line), "Temp: %.1fC", s_temp);
    } else {
        snprintf(line, sizeof(line), "Temp: --.-C");
    }
    draw_centered(TEMP_Y, line, 2);
    if (s_have_sensor) {
        snprintf(line, sizeof(line), "Hum: %.1f%%", s_hum);
    } else {
        snprintf(line, sizeof(line), "Hum: --.-%%");
    }
    draw_centered(HUM_Y, line, 2);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    s_time_ok = tmv.tm_year >= (2020 - 1900);
    if (s_time_ok) {
        char date[12];
        strftime(date, sizeof(date), "%Y-%m-%d", &tmv);
        draw_centered(DATE_Y, date, 1);
    }
}

/* 每秒局部刷新时间行（同参考 updateDateTime 的 fillRect 流程） */
static void redraw_clock(void)
{
    const char *s = "--:--:--";
    char buf[9];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_year >= (2020 - 1900)) {
        strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
        s = buf;
    }
    st7735_fill_rect(0, TIME_Y - 2, ST7735_W, 18, s_bg);
    draw_centered(TIME_Y, s, 2);
}

static void time_sync_cb(struct timeval *tv)
{
    struct tm tmv;
    localtime_r(&tv->tv_sec, &tmv);
    ESP_LOGI(TAG, "SNTP 已同步：%04d-%02d-%02d %02d:%02d:%02d UTC+8",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

/* 联网后启动 SNTP：内网 NTP（参考 app_config.h）+ pool.ntp.org 兜底，UTC+8 */
static void start_sntp_once(void)
{
    static bool started;
    if (started) {
        return;
    }
    started = true;
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "192.168.63.1");
    esp_sntp_setservername(1, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP 启动（192.168.63.1 + pool.ntp.org，UTC+8）");
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == IP_EVENT_STA_GOT_IP) {
        start_sntp_once();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(st7735_init());
    st7735_fill_screen(COLOR_BLACK);
    draw_centered(TEMP_Y - 8, "weather-clock", 1);
    draw_centered(HUM_Y, "AHT30 init...", 1);

    esp_err_t aerr = aht30_init();
    if (aerr != ESP_OK) {
        /* 参考工程失败即死循环；这里不阻塞联网与 OTA，5 秒拍里重试 */
        ESP_LOGW(TAG, "AHT30 初始化失败：%s，稍后重试", esp_err_to_name(aerr));
    }

    /* 板端维护页 :80（WiFi 配网 / OTA 刷机 / 状态），自带 APSTA 热点兜底 */
    app_web_init();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               ip_event_handler, NULL));

    /* 主循环看门狗：1s 一拍喂狗，卡死 >5s 触发 panic 重启自恢复（同 blink）。
     * IDF 启动期已按 sdkconfig 默认初始化 TWDT，这里 reconfigure 收编 */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t werr = esp_task_wdt_reconfigure(&wdt_cfg);
    if (werr != ESP_OK) {
        werr = esp_task_wdt_init(&wdt_cfg);
    }
    if (werr != ESP_OK && werr != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(werr);
    }
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    int sec10 = 10, color50 = 50; /* 首拍立即出画面 */
    while (true) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
        if (++sec10 >= 10) {
            sec10 = 0;
            redraw_clock();
        }
        if (++color50 >= 50) {
            color50 = 0;
            if (!s_have_sensor) {
                aht30_init(); /* 幂等：未就绪（含开机失败）时补初始化 */
            }
            if (aht30_read(&s_temp, &s_hum) == ESP_OK) {
                if (!s_have_sensor) {
                    ESP_LOGI(TAG, "AHT30 读数恢复");
                }
                s_have_sensor = true;
            } else {
                s_have_sensor = false;
            }
            repaint();
            ESP_LOGI(TAG, "%s T=%.1fC RH=%.1f%% heap=%uK",
                     s_have_sensor ? "read ok" : "sensor fail",
                     s_temp, s_hum, (unsigned)(esp_get_free_heap_size() / 1024));
        }
    }
}
