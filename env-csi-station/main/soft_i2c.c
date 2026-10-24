/* soft_i2c — 软件模拟 I2C 主机（位带实现，仅写事务）。
 * 时序：半周期 2µs（~250kHz），esp_rom_delay_us 精确节拍；START/STOP 按
 * I2C 规范（SCL 高时 SDA 跳变）；每字节 MSB 先行，SDA 在 SCL 低时摆好，
 * 第 9 个时钟释放 SDA 读 ACK（低=应答）。无上拉检测：init 及每次事务前
 * 释放双线，SCL 仍为低 → 缓存无上拉态，写路径永久短路（重启恢复）。 */
#include "soft_i2c.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "softi2c";

#define HALF_US 10 /* 半周期 10µs ≈ 50kHz：上拉可能仅内部 ~45k（模块自带上拉
                    * 缺席/未供电），回摆 τ 数 µs，250kHz 会漏采；刷新 ~0.4s/帧可接受 */

static int s_sda = -1;
static int s_scl = -1;
static bool s_no_pullup;

static void scl_lo(void) { gpio_set_level((gpio_num_t)s_scl, 0); }
static void scl_hi(void) { gpio_set_level((gpio_num_t)s_scl, 1); }
static void sda_lo(void) { gpio_set_level((gpio_num_t)s_sda, 0); }
static void sda_hi(void) { gpio_set_level((gpio_num_t)s_sda, 1); }

void soft_i2c_init(int sda_io, int scl_io)
{
    s_sda = sda_io;
    s_scl = scl_io;
    s_no_pullup = false; /* 重新 init 清缓存态（自诊断扫描会换线序重试） */
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda_io) | (1ULL << scl_io),
        /* 开漏：输出高=释放，靠上拉回高。必须 INPUT_OUTPUT_OD：C3 输入缓冲
         * （IE）关闭时 gpio_get_level 恒读 0 → 假"总线无上拉"→ 写路径被
         * 永久短路（2026-10-24 自诊断实证：同脚 INPUT 读 H、OUTPUT_OD 读 L） */
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE, /* 内部上拉兜底（模块通常自带 4.7-10k） */
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    sda_hi();
    scl_hi();
    esp_rom_delay_us(5 * HALF_US); /* 释放后静置 50µs：弱上拉回摆留足时间 */
    if (gpio_get_level((gpio_num_t)s_scl) == 0) {
        s_no_pullup = true;
        ESP_LOGW(TAG, "总线无上拉（SCL=%d 恒低）：屏未接/未供电？写路径短路", s_scl);
    }
}

int soft_i2c_bus_check(int sda_io, int scl_io)
{
    if (sda_io < 0 || sda_io >= GPIO_NUM_MAX || scl_io < 0 || scl_io >= GPIO_NUM_MAX) {
        return -1;
    }
    /* 双线开漏+内部上拉并释放（输出高=释放），与 init 相同的电气条件，
     * 但不触碰全局 s_sda/s_scl/s_no_pullup——纯体检，探测扫描可反复调用 */
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda_io) | (1ULL << scl_io),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level((gpio_num_t)sda_io, 1);
    gpio_set_level((gpio_num_t)scl_io, 1);
    int mask = 3;
    for (int try = 0; try < 5; try++) { /* ≤5 次，重试间隔 10ms */
        esp_rom_delay_us(2000); /* 静置 ≥2ms：弱上拉回摆留足时间 */
        mask = (gpio_get_level((gpio_num_t)sda_io) ? 0 : 1)
             | (gpio_get_level((gpio_num_t)scl_io) ? 0 : 2);
        if (mask == 0) {
            return 0; /* 双线皆高：总线可用 */
        }
        if (try < 4) {
            esp_rom_delay_us(10000);
        }
    }
    return mask; /* bit0=SDA 恒低；bit1=SCL 恒低 */
}

int soft_i2c_write(const uint8_t *buf, int len)
{
    if (!buf || len <= 0 || s_sda < 0 || s_no_pullup) {
        return -1;
    }
    /* 事务前总线空闲检查：释放双线后 SCL 仍低 → 缓存无上拉态（50µs 回摆余量） */
    scl_hi();
    sda_hi();
    esp_rom_delay_us(50);
    if (gpio_get_level((gpio_num_t)s_scl) == 0) {
        s_no_pullup = true;
        ESP_LOGW(TAG, "总线无上拉，写路径短路");
        return -1;
    }

    /* START：SCL 高时 SDA 由高→低 */
    sda_lo();
    esp_rom_delay_us(HALF_US);
    scl_lo();
    esp_rom_delay_us(HALF_US);

    int nack = 0;
    for (int i = 0; i < len && nack == 0; i++) {
        const uint8_t b = buf[i];
        for (int k = 7; k >= 0; k--) { /* SCL 低时摆 SDA，SCL 高采样 */
            if (b & (1u << k)) sda_hi(); else sda_lo();
            esp_rom_delay_us(HALF_US);
            scl_hi();
            esp_rom_delay_us(HALF_US);
            scl_lo();
            esp_rom_delay_us(HALF_US);
        }
        /* 第 9 个时钟：释放 SDA 读 ACK（低=应答，高=NACK） */
        sda_hi();
        esp_rom_delay_us(HALF_US);
        scl_hi();
        esp_rom_delay_us(HALF_US);
        if (gpio_get_level((gpio_num_t)s_sda) != 0) {
            nack = i + 1; /* 第 i+1 个字节无应答 */
        }
        scl_lo();
        esp_rom_delay_us(HALF_US);
    }

    /* STOP：SCL 高时 SDA 由低→高 */
    sda_lo();
    esp_rom_delay_us(HALF_US);
    scl_hi();
    esp_rom_delay_us(HALF_US);
    sda_hi();
    esp_rom_delay_us(HALF_US);
    return nack;
}
