/* aht30 — AHT30 温湿度传感器（I2C 地址 0x38），协议同 AHT20：
 * 读状态字节查校准位（status & 0x18 == 0x18），未校准发 0xE1 0x08 0x00；
 * 测量触发 0xAC 0x33 0x00，80ms 转换后读 7 字节（状态 + 20bit 湿度 + 20bit 温度）。
 * 接线沿用参考工程（Arduino C3 默认 Wire 引脚）：SDA=GPIO8、SCL=GPIO9；
 * 注意 SCL=GPIO9 兼 BOOT 键——按键按下会拉低 SCL 令当次读数失败，忽略即可。
 */
#include "aht30.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AHT30_SDA_GPIO GPIO_NUM_8
#define AHT30_SCL_GPIO GPIO_NUM_9
#define AHT30_I2C_ADDR 0x38

static const char *TAG = "aht30";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

esp_err_t aht30_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK; /* 已初始化：总线/设备句柄复用，避免重复 acquire */
    }
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AHT30_SDA_GPIO,
        .scl_io_num = AHT30_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* 模块自带上拉，内部上拉兜底 */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_bus), TAG, "i2c bus init");

    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT30_I2C_ADDR,
        .scl_speed_hz = 100 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev, &s_dev), TAG,
                        "i2c dev add");

    vTaskDelay(pdMS_TO_TICKS(100)); /* 上电稳定（datasheet 100ms） */
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(i2c_master_receive(s_dev, &status, 1, 100), TAG,
                        "read status");
    if ((status & 0x18) != 0x18) {
        uint8_t init_cmd[3] = { 0xE1, 0x08, 0x00 };
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, init_cmd, 3, 100), TAG,
                            "calibrate");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "AHT30 就绪：SDA=%d SCL=%d status=0x%02X",
             AHT30_SDA_GPIO, AHT30_SCL_GPIO, status);
    return ESP_OK;
}

esp_err_t aht30_read(float *temp_c, float *hum_pct)
{
    uint8_t trig[3] = { 0xAC, 0x33, 0x00 };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, trig, 3, 100), TAG, "trigger");
    vTaskDelay(pdMS_TO_TICKS(80)); /* 转换等待（datasheet 80ms） */
    uint8_t d[7] = { 0 };
    ESP_RETURN_ON_ERROR(i2c_master_receive(s_dev, d, 7, 100), TAG, "read");
    if (d[0] & 0x80) {
        return ESP_ERR_INVALID_STATE; /* 忙位仍置位 */
    }
    uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t raw_t = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    *hum_pct = raw_h * 100.0f / 1048576.0f;
    *temp_c = raw_t * 200.0f / 1048576.0f - 50.0f;
    return ESP_OK;
}
