#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* AHT20 温湿度传感器（I2C 0x38）。
 * aht20_start() 建 bus+device 并做上电初始化（软复位 + 校准位检查）；
 * 探测失败（无 ACK）返回错误——调用方可换线序重建（main.c 的对调重试）。 */
esp_err_t aht20_start(int sda_io, int scl_io);

/* 触发测量并读取（阻塞 ~90ms）。CRC 校验失败返回 ESP_ERR_INVALID_CRC。 */
esp_err_t aht20_read(float *temp_c, float *rh);

/* 总线句柄出口：SSD1306 可与 AHT20 共线（0x3C vs 0x38 不冲突），供
 * main.c 的 OLED hw-bus 扫描（i2c_master_probe / add_device）用；
 * aht20_start 失败时返回 NULL。50kHz、内部上拉（勿改速——杜邦线真机教训）。 */
i2c_master_bus_handle_t aht20_bus(void);
