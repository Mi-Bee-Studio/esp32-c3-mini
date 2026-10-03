#pragma once

#include <esp_err.h>

/* AHT30 温湿度传感器（I2C 0x38），协议同 AHT20 */
esp_err_t aht30_init(void);
esp_err_t aht30_read(float *temp_c, float *hum_pct);
