#pragma once
#include <stdint.h>
/* 软件模拟 I2C 主机（位带，SSD1306 专用写路径）。
 * 背景：C3 仅一路硬件 I2C（I2C0 已给 AHT20 8/9），SSD1306 走软 I2C 4/6。
 * 开漏+输入（INPUT_OUTPUT_OD）+ 内部上拉（模块通常自带 4.7-10k 上拉），
 * ~50kHz 保守档（上拉可能仅内部 ~45k），无时钟拉伸支持。C3 输入缓冲必须
 * 使能：IE 关闭时 gpio_get_level 恒读 0（假"总线无上拉"），见 soft_i2c.c。 */
void soft_i2c_init(int sda_io, int scl_io);
/* 总线体检（不依赖 init 状态，双线 INPUT_OUTPUT_OD+内部上拉，释放后静置
 * ≥2ms 采样，≤5 次重试间隔 10ms）：返回 0=双线皆高（总线可用）；
 * bit0=SDA 恒低；bit1=SCL 恒低；-1=参数非法。接线排障专用（oled 自诊断）。 */
int soft_i2c_bus_check(int sda_io, int scl_io);
/* 写事务：START + buf 全部字节（首字节=写地址，如 0x3C<<1）+ ACK 位 + STOP。
 * 返回 0=成功；n>0=第 n 个字节 NACK；-1=总线无上拉（SCL 释放后仍为低）。 */
int soft_i2c_write(const uint8_t *buf, int len);
