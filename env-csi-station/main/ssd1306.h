#pragma once
#include <stdbool.h>
#include <stdint.h>
/* SSD1306 128×64 驱动：自 esp32-s3-zero/env-station 同构拷贝，传输层已抽象
 * 为函数指针（buf 为裸载荷：首字节 D/C 控制字节 0x00/0x40 + 命令/数据），
 * 地址由传输层负责（软 I2C 前插写地址字节；hw I2C 由 device 句柄承载）。
 * 现有两传输：main.c 的 soft_i2c(4/6) 与 hw I2C0(8/9，与 AHT20 共线)。 */

/* 传输层写函数：成功返回 0，非 0 = 失败（无应答/总线不可用）。 */
typedef int (*ssd1306_write_fn)(const uint8_t *buf, int len);

/* 绑定传输层并初始化（含 0xAE 探测命令，无应答返回 false=无屏继续跑）。 */
bool ssd1306_init(ssd1306_write_fn write_fn, uint8_t addr);
void ssd1306_clear(void);
void ssd1306_text(int col, int row, const char *s, int scale);
void ssd1306_hline(int x0, int x1, int y);
void ssd1306_vline(int x, int y0, int y1);
void ssd1306_fill_rect(int x, int y, int w, int h);
void ssd1306_flush(void);
void ssd1306_display_on(bool on);
int ssd1306_errs(void); /* 累计传输失败数（诊断用） */
