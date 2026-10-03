#pragma once

/* st7735 — ST7735 0.96" 160x80 mini TFT（硬件 SPI），rotation 3 横屏。
 * 引脚沿用参考 Arduino 工程 app_config.h：SCLK=4 MOSI=6 CS=7 DC=5 RST=3 BL=10。 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ST7735_W 160
#define ST7735_H 80

#define RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8) << 8) | (((uint16_t)(g) & 0xFC) << 3) | ((uint8_t)(b) >> 3)))

esp_err_t st7735_init(void);
void st7735_backlight(bool on);
void st7735_fill_screen(uint16_t color);
void st7735_fill_rect(int x, int y, int w, int h, uint16_t color);
/* 5x7 字库缩放渲染；bg 直接写入（覆盖式，无闪烁），返回无值 */
void st7735_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
/* 像素宽度（5px 字形 + 1px 字距，按 scale 缩放，不含末尾空隙） */
int st7735_text_width(const char *s, int scale);
