#pragma once
#include <stdbool.h>
#include <stdint.h>
/* SSD1306 128×64 软 I2C 版：自 esp32-s3-zero/env-station 同构拷贝，
 * 传输层由硬件 I2C master 换接 soft_i2c（C3 单硬件 I2C 已被 AHT20 占用）。API 语义不变。 */
bool ssd1306_init(int sda_io, int scl_io, uint8_t addr); /* 返回 false=无应答/无上拉 */
void ssd1306_clear(void);
void ssd1306_text(int col, int row, const char *s, int scale);
void ssd1306_hline(int x0, int x1, int y);
void ssd1306_vline(int x, int y0, int y1);
void ssd1306_fill_rect(int x, int y, int w, int h);
void ssd1306_flush(void);
void ssd1306_display_on(bool on);
int ssd1306_errs(void); /* 累计传输失败数（诊断用） */
