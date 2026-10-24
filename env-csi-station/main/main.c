/*
 * env-csi-station — AHT20 温湿度 + WiFi CSI 采集站（nologo ESP32-C3 SuperMini）。
 * 当前外设：AHT20 @ 硬 I2C0（板丝印 SDA/SCL = GPIO8/9）+ HC-SR501 人体感应
 * （OUT→GPIO4，见 app_pir.c）。SSD1306 屏已拆走单独调试：开机仅扫硬 I2C0
 * （8/9）×{0x3C,0x3D}——屏的回归位（原软 I2C 4/6 扫描已收敛移除，GPIO4
 * 现归 PIR；soft_i2c.c 保留编译备用、开机不再触碰）。
 * 板级事实：GPIO8 兼板载蓝灯（低电平点亮 + 10k 上拉），I2C 流量会让
 * 蓝灯微闪属正常；GPIO9=BOOT 键，运行中按下会毁一次 I2C 读（自动重试）。
 * 屏显五区（见 draw_ui）：标题/RSSI、温度、湿度、信道带宽包率、CSI 波形。
 * WiFi 关联后 app_net→app_csi 开 CSI + 1Hz 网关 ping 激励（ACK 帧出样本）。
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "app_aht20.h"
#include "app_csi.h"
#include "app_net.h"
#include "app_pir.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "ssd1306.h"

static const char *TAG = "MAIN";

/* AHT20 引脚（硬 I2C0 = 板丝印 SDA/SCL 8/9）。SSD1306 屏已拆走单独调试，
 * 回归位 = 同线共线（8/9），开机自扫 {0x3C,0x3D}；原软 I2C(4/6) 扫描已
 * 收敛移除——GPIO4 现归 HC-SR501 PIR（见 app_pir.h），GPIO6 空闲。 */
#define AHT_SDA  8
#define AHT_SCL  9

static bool s_oled_ok;
static char s_oled_loc[20] = "err";       /* 位置串：err/hw:0x3C[/hw:0x3D] */
static i2c_master_dev_handle_t s_oled_dev; /* hw I2C0 上的 SSD1306 device */
static bool s_aht_ok;
static bool s_env_valid;   /* 至少成功读成一次 AHT20 */
static float s_temp, s_rh; /* 最近一次成功读数（读失败保留旧值） */
static uint32_t s_env_errs;

/* ---------------------------------------------------------------------------
 * app_net.c（/api/status 状态卡）同源数据出口
 * ------------------------------------------------------------------------- */
bool station_env(float *t, float *rh)
{
    if (t) {
        *t = s_temp;
    }
    if (rh) {
        *rh = s_rh;
    }
    return s_env_valid;
}

uint32_t station_csi_pkts(void)
{
    csi_status_t st;
    app_csi_get_status(&st);
    return st.pkts;
}

float station_csi_hz(void)
{
    csi_status_t st;
    app_csi_get_status(&st);
    return st.rate_hz;
}

/* ---------------------------------------------------------------------------
 * 开机 WiFi 扫描行（SELFTEST 规范第三行）。APSTA 下扫描合法：IDF 会让
 * STA 临时离开信道扫完全信道再回（blink 家族既定行为，未配网也照扫）。
 * ------------------------------------------------------------------------- */
static void scan_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000)); /* 先让 STA 关联流程走起来 */

    wifi_scan_config_t cfg = { 0 };
    esp_wifi_scan_start(&cfg, true); /* 阻塞扫全信道 */

    wifi_ap_record_t recs[20];
    uint16_t n = 20;
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        n = 0;
    }
    /* 返回已按 RSSI 降序：拼最强 5 条 "RSSI ch SSID"，"; " 连接 */
    char top[256] = "";
    for (int i = 0; i < n && i < 5; i++) {
        char one[64];
        snprintf(one, sizeof(one), "%s%d ch%d %.24s", i ? "; " : "",
                 recs[i].rssi, recs[i].primary, (const char *)recs[i].ssid);
        strlcat(top, one, sizeof(top));
    }

    /* ours：配置网 SSID=RSSI——关联中用关联 AP；否则用扫描里同名最强 BSS；
     * 都没有则空（ssid 为空也空） */
    char ours[64] = "";
    char ssid[33] = "";
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
        size_t sl = sizeof(ssid);
        if (nvs_get_str(h, "ssid", ssid, &sl) != ESP_OK) {
            ssid[0] = '\0';
        }
        nvs_close(h);
    }
    if (ssid[0]) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            snprintf(ours, sizeof(ours), "%s=%d", ssid, ap.rssi);
        } else {
            for (int i = 0; i < n; i++) {
                if (strcmp((const char *)recs[i].ssid, ssid) == 0) {
                    snprintf(ours, sizeof(ours), "%s=%d", ssid, recs[i].rssi);
                    break;
                }
            }
        }
    }
    ESP_LOGI(TAG, "SELFTEST: wifi aps=%u top=\"%s\" ours=\"%s\"",
             (unsigned)n, top, ours);
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------------------
 * 屏显五区布局（128×64）：
 *   y0-7   标题 ENV+CSI + 右上 RSSI/NOAP
 *   y9     分隔线
 *   y11-26 温度（scale2）· 湿度（scale2，x=78 起）
 *   y28-36 CHx BWxx xPK/S（关联）或 AP 192.168.4.1（未配网）
 *   y38    分隔线
 *   y40-63 CSI 幅度波形（24px 高；n<2 或未关联给占位文案）
 * ------------------------------------------------------------------------- */
static void draw_ui(const csi_status_t *st, const float *wave, int n)
{
    ssd1306_clear();

    wifi_ap_record_t ap;
    const bool assoc = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;

    /* 区 1：标题 + RSSI */
    ssd1306_text(0, 0, "ENV+CSI", 1);
    if (assoc) {
        char rs[8];
        snprintf(rs, sizeof(rs), "%d", ap.rssi);
        ssd1306_text(106, 0, rs, 1);
    } else {
        ssd1306_text(106, 0, "NOAP", 1);
    }
    ssd1306_hline(0, 127, 9);

    /* 区 2/3：温（y11-26，scale2）· 湿（x=78，scale2） */
    char v[16];
    if (s_env_valid) {
        snprintf(v, sizeof(v), "%.1f\x80" "C", s_temp);
    } else {
        strlcpy(v, "--.-\x80" "C", sizeof(v));
    }
    ssd1306_text(0, 11, v, 2);
    if (s_env_valid) {
        snprintf(v, sizeof(v), "%.1f%%", s_rh);
    } else {
        strlcpy(v, "--%", sizeof(v));
    }
    ssd1306_text(78, 11, v, 2);

    /* 区 4：信道/带宽/CSI 包率 或 未配网提示 */
    char line[32];
    if (assoc) {
        snprintf(line, sizeof(line), "CH%u BW%u %dPK/S",
                 (unsigned)ap.primary,
                 ap.second == WIFI_SECOND_CHAN_NONE ? 20u : 40u,
                 (int)(st->rate_hz + 0.5f));
    } else {
        strlcpy(line, "AP 192.168.4.1", sizeof(line));
    }
    ssd1306_text(0, 28, line, 1);
    ssd1306_hline(0, 127, 38);

    /* 区 5：CSI 波形（y40-63，22px 高 + 基线） */
    if (!assoc) {
        ssd1306_text(16, 50, "CSI WAIT ASSOC", 1);
        return;
    }
    if (n < 2) {
        ssd1306_text(16, 50, "NO CSI PKTS", 1);
        return;
    }
    float mn = wave[0], mx = wave[0];
    for (int i = 1; i < n; i++) {
        if (wave[i] < mn) {
            mn = wave[i];
        }
        if (wave[i] > mx) {
            mx = wave[i];
        }
    }
    float span = mx - mn;
    const bool flat = span < 1e-3f; /* 静置平坦：画中线，避免除零 */
    if (!flat) {
        const float pad = span * 0.05f; /* 5% 双侧余量 */
        mn -= pad;
        mx += pad;
        span = mx - mn;
    }
    for (int i = 0; i < n && i < 128; i++) {
        int hgt;
        if (flat) {
            hgt = 11; /* 中线 */
        } else {
            hgt = (int)(22.0f * (wave[i] - mn) / span + 0.5f);
            if (hgt < 0) {
                hgt = 0;
            }
            if (hgt > 22) {
                hgt = 22;
            }
        }
        ssd1306_vline(i, 63 - hgt, 63); /* n<128 时 x=0..n-1，最新在右端 */
    }
}

/* ---------------------------------------------------------------------------
 * ssd1306 传输层（buf 为裸载荷：首字节 D/C 控制字节 0x00/0x40；器件地址
 * 由 hw I2C device 句柄承载）。原软 I2C 传输层随软总线扫描一起移除——
 * soft_i2c.c 仍编译保留，待屏回来若再需独立总线可复用。
 * ------------------------------------------------------------------------- */
static int oled_hw_write(const uint8_t *buf, int len)
{
    /* 与 AHT20 同速共线（50kHz：内部上拉弱，杜邦线真机教训），超时 100ms */
    return i2c_master_transmit(s_oled_dev, buf, len, 100) == ESP_OK ? 0 : -1;
}

void app_main(void)
{
    /* a. AHT20 @ 硬 I2C0（8/9）先起：OLED 自适应扫描要用它的总线句柄
     *    （屏可能与 AHT20 共线——SuperMini 板丝印 SDA/SCL 即这两脚） */
    s_aht_ok = aht20_start(AHT_SDA, AHT_SCL) == ESP_OK;
    if (!s_aht_ok) {
        ESP_LOGW(TAG, "AHT20 探测失败：查 VCC/GND/线序；GPIO9=BOOT，按住会拉低 SCL");
    }

    /* b. OLED 上电探测（屏已拆走单独调试——扫的是回归位）：仅硬 I2C0
     *    （8/9 板丝印 SDA/SCL，与 AHT20 共线）× 双地址 {0x3C,0x3D}，首个
     *    ACK 即驱动（0x3C 屏与 0x38 AHT20 不冲突）。原软 I2C(4/6)×双线序
     *    ×双地址全扫描已收敛移除：GPIO4 现归 HC-SR501 PIR（app_pir）。
     *    soft_i2c.c 仍编译保留（未来外设复用软 I2C），开机不再触碰。
     *    全 NACK = 无屏模式继续跑（单条 W，不刷屏）。 */
    {
        static const uint8_t oled_addrs[2] = {0x3C, 0x3D};
        i2c_master_bus_handle_t bus = aht20_bus();
        for (int a = 0; a < 2 && !s_oled_ok && bus != NULL; a++) {
            const uint8_t addr = oled_addrs[a];
            const char *probe =
                i2c_master_probe(bus, addr, 50) == ESP_OK ? "ack" : "nack";
            ESP_LOGI(TAG, "OLED: try bus=hw sda=%d scl=%d addr=0x%02X line=%s probe=%s",
                     AHT_SDA, AHT_SCL, addr, s_aht_ok ? "ok" : "err", probe);
            if (strcmp(probe, "ack") == 0) {
                const i2c_device_config_t dev_cfg = {
                    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                    .device_address = addr,
                    .scl_speed_hz = 50 * 1000, /* 与 AHT20 同速（共线勿改） */
                };
                if (i2c_master_bus_add_device(bus, &dev_cfg, &s_oled_dev) == ESP_OK) {
                    s_oled_ok = ssd1306_init(oled_hw_write, addr);
                    if (s_oled_ok) {
                        snprintf(s_oled_loc, sizeof(s_oled_loc), "hw:0x%02X", addr);
                    }
                    ESP_LOGI(TAG, "OLED: 使用 hw I2C0(8/9 板丝印SDA/SCL) @0x%02X（%s）",
                             addr, s_oled_ok ? "就绪" : "初始化失败");
                } else {
                    ESP_LOGW(TAG, "OLED: hw device 添加失败 @0x%02X", addr);
                }
            }
        }
        if (bus == NULL) {
            ESP_LOGW(TAG, "OLED: hw I2C0 总线不可用（aht20_start 失败），跳过扫描");
        }
        if (!s_oled_ok) {
            strlcpy(s_oled_loc, "err", sizeof(s_oled_loc));
            ESP_LOGW(TAG, "OLED: 无应答（屏拆走单独调试期间正常），无屏模式继续跑");
        }
    }
    if (s_oled_ok) {
        ssd1306_clear();
        ssd1306_text(28, 20, "ENV+CSI", 2);
        ssd1306_text(22, 40, "BOOTING...", 1);
        ssd1306_flush();
    }

    /* c. HC-SR501 PIR @ GPIO4（ANYEDGE 中断+3s 去抖计数、60s 预热、120s
     *    在场窗；ISR 只计数，主循环 app_pir_poll 消费打点） */
    app_pir_init();

    /* d. TWDT（家族模式：5s panic；重复 init 容忍 INVALID_STATE） */
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

    /* e. SELFTEST 身份行（blink 模板 + flash/sensor/uid 扩展；PSRAM 探测
     *    沿用 heap_caps 侧总量机制——无 PSRAM 板返回 0 → none） */
    {
        esp_chip_info_t ci;
        esp_chip_info(&ci);
        size_t psz = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        char psram_str[12];
        if (psz) {
            snprintf(psram_str, sizeof(psram_str), "%uMB", (unsigned)(psz >> 20));
        } else {
            strlcpy(psram_str, "none", sizeof(psram_str));
        }
        uint32_t fsz = 0;
        esp_flash_get_size(NULL, &fsz);
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        ESP_LOGI(TAG, "SELFTEST: board=esp32-c3-mini-env-csi-station fw=v0.1.0"
                      " chip=%s rev=v%d.%d cores=%u flash=%uMB psram=%s"
                      " sensor=%s heap=%uKB uid=%02x%02x%02x%02x%02x%02x",
                 CONFIG_IDF_TARGET, ci.revision / 100, ci.revision % 100,
                 ci.cores, (unsigned)(fsz >> 20), psram_str,
                 s_aht_ok ? "AHT20+SR501" : "SR501",
                 (unsigned)(esp_get_free_heap_size() >> 10),
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    /* f. SELFTEST 传感器行（aht20 ok/err；ssd1306 报落点 hw:0x3C/err——屏已
     *    拆走，扫的是回归位硬 I2C0；pir 为 GPIO 器件（开机恒预热期），简报
     *    引脚号即可） */
    ESP_LOGI(TAG, "SELFTEST: i2c aht20=%s ssd1306=%s pir=gpio%d",
             s_aht_ok ? "ok" : "err", s_oled_loc, PIR_GPIO_NUM);

    /* g. 网络（APSTA+Web 配网/OTA）→ CSI → 开机扫描行（延迟 3s） */
    app_net_init();
    app_csi_init();
    xTaskCreate(scan_task, "scan", 4096, NULL, 3, NULL);

    /* h. 主循环 1s 一拍：喂狗 → 2s 一采 AHT20（失败保留旧值）→ CSI 快照
     *    → 刷屏 → PIR 事件消费 → 30s 心跳 */
    uint32_t tick = 0;
    csi_status_t st;
    static float wave[128];

    while (true) {
        esp_task_wdt_reset();

        if (tick % 2 == 0) {
            float t, rh;
            if (aht20_read(&t, &rh) == ESP_OK) {
                s_temp = t;
                s_rh = rh;
                s_env_valid = true;
            } else {
                s_env_errs++; /* 保留旧值继续显示/上报 */
            }
        }

        app_csi_get_status(&st);
        const int n = app_csi_waveform(wave);

        if (s_oled_ok) {
            draw_ui(&st, wave, n);
            ssd1306_flush();
        }

        app_pir_poll(); /* PIR：主循环消费运动事件（≥3s 节流打点，ISR 不打日志） */

        if (tick % 30 == 29) {
            pir_status_t pir;
            app_pir_get_status(&pir);
            ESP_LOGI(TAG, "heartbeat uptime=%llds heap=%uK csi=%lu pkts"
                          " @%.1f/s temp=%.1f rh=%.1f pir=%u last=%ds"
                          " present=%d",
                     (long long)(esp_timer_get_time() / 1000000),
                     (unsigned)(esp_get_free_heap_size() >> 10),
                     (unsigned long)st.pkts, st.rate_hz, s_temp, s_rh,
                     (unsigned)pir.count, pir.last_trig_s,
                     pir.present ? 1 : 0);
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
