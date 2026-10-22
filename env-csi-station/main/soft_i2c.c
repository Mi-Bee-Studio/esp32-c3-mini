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

#define HALF_US 2 /* 半周期 2µs ≈ 250kHz（上拉偏弱时保守） */

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
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda_io) | (1ULL << scl_io),
        .mode = GPIO_MODE_OUTPUT_OD, /* 开漏：输出高=释放，靠上拉回高 */
        .pull_up_en = GPIO_PULLUP_ENABLE, /* 内部上拉兜底（模块通常自带 4.7-10k） */
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    sda_hi();
    scl_hi();
    esp_rom_delay_us(5 * HALF_US);
    if (gpio_get_level((gpio_num_t)s_scl) == 0) {
        s_no_pullup = true;
        ESP_LOGW(TAG, "总线无上拉（SCL=%d 恒低）：屏未接/未供电？写路径短路", s_scl);
    }
}

int soft_i2c_write(const uint8_t *buf, int len)
{
    if (!buf || len <= 0 || s_sda < 0 || s_no_pullup) {
        return -1;
    }
    /* 事务前总线空闲检查：释放双线后 SCL 仍低 → 缓存无上拉态 */
    scl_hi();
    sda_hi();
    esp_rom_delay_us(HALF_US);
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
