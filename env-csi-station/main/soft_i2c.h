#pragma once
#include <stdint.h>
/* 软件模拟 I2C 主机（位带，SSD1306 专用写路径）。
 * 背景：C3 仅一路硬件 I2C（I2C0 已给 AHT20 8/9），SSD1306 走软 I2C 4/6。
 * 开漏输出 + 内部上拉（模块通常自带 4.7-10k 上拉），~250kHz，无时钟拉伸支持。 */
void soft_i2c_init(int sda_io, int scl_io);
/* 写事务：START + buf 全部字节（首字节=写地址，如 0x3C<<1）+ ACK 位 + STOP。
 * 返回 0=成功；n>0=第 n 个字节 NACK；-1=总线无上拉（SCL 释放后仍为低）。 */
int soft_i2c_write(const uint8_t *buf, int len);
