/* st7735 — ST7735 0.96" 160x80 mini TFT 驱动（硬件 SPI，rotation 3 横屏）。
 *
 * 初始化序列复刻 Adafruit_ST7735 initR(INITR_MINI160x80)（Rcmd1 +
 * Rcmd2green160x80 + Rcmd3 + RGB 滤色 MADCTL），参考工程同款面板；
 * rotation 3 → MADCTL = MX|MV|RGB = 0x60，X 偏移 _rowstart=0、Y 偏移
 * _colstart=24（面板 RAM 132x162 居中取窗）。
 * 绘图走 esp_lcd panel_io 同步事务；行缓冲 160 像素按行推显，
 * DMA 容量内无整帧 framebuffer（C3 无 PSRAM，省 32KB）。
 */
#include "st7735.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "font5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 引脚 = 参考工程 app_config.h 的接线（TFT_BL 10, CS 7, DC 5, RST 3, MOSI 6, SCLK 4） */
#define LCD_SCLK_GPIO GPIO_NUM_4
#define LCD_MOSI_GPIO GPIO_NUM_6
#define LCD_CS_GPIO   GPIO_NUM_7
#define LCD_DC_GPIO   GPIO_NUM_5
#define LCD_RST_GPIO  GPIO_NUM_3
#define LCD_BL_GPIO   GPIO_NUM_10

/* ST7735 命令（Adafruit_ST77xx.h / Adafruit_ST7735.h 同名） */
#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_NORON   0x13
#define CMD_INVOFF  0x20
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C
#define CMD_MADCTL  0x36
#define CMD_COLMOD  0x3A
#define CMD_FRMCTR1 0xB1
#define CMD_FRMCTR2 0xB2
#define CMD_FRMCTR3 0xB3
#define CMD_INVCTR  0xB4
#define CMD_PWCTR1  0xC0
#define CMD_PWCTR2  0xC1
#define CMD_PWCTR3  0xC2
#define CMD_PWCTR4  0xC3
#define CMD_PWCTR5  0xC4
#define CMD_VMCTR1  0xC5
#define CMD_GMCTRP1 0xE0
#define CMD_GMCTRN1 0xE1

/* rotation 3 画面偏移：X=0、Y=24（initR MINI160x80 的 _rowstart/_colstart） */
#define X_OFF 0
#define Y_OFF 24

static const char *TAG = "st7735";
static esp_lcd_panel_io_handle_t s_io;
static uint16_t s_line[ST7735_W]; /* 行缓冲（RGB565） */

static void lcd_cmd(uint8_t cmd, const uint8_t *params, size_t n, uint32_t delay_ms)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, cmd, params, n));
    if (delay_ms) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* 设写窗（CASET/RASET/RAMWR），随后 tx_color(-1) 连续推数据，窗内指针自增 */
static void set_window(int x, int y, int w, int h)
{
    uint8_t caset[4] = {
        (uint8_t)((X_OFF + x) >> 8), (uint8_t)(X_OFF + x),
        (uint8_t)((X_OFF + x + w - 1) >> 8), (uint8_t)(X_OFF + x + w - 1),
    };
    uint8_t raset[4] = {
        (uint8_t)((Y_OFF + y) >> 8), (uint8_t)(Y_OFF + y),
        (uint8_t)((Y_OFF + y + h - 1) >> 8), (uint8_t)(Y_OFF + y + h - 1),
    };
    lcd_cmd(CMD_CASET, caset, 4, 0);
    lcd_cmd(CMD_RASET, raset, 4, 0);
    lcd_cmd(CMD_RAMWR, NULL, 0, 0);
}

static void push_line(int w)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, -1, s_line, w * sizeof(uint16_t)));
}

esp_err_t st7735_init(void)
{
    const gpio_config_t io_out = {
        .pin_bit_mask = (1ULL << LCD_BL_GPIO) | (1ULL << LCD_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io_out));
    gpio_set_level(LCD_BL_GPIO, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .sclk_io_num = LCD_SCLK_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* 单行最大推显 + 余量；10 MHz 对杜邦接线稳妥 */
        .max_transfer_sz = ST7735_W * 2 * 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = LCD_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = 10 * 1000 * 1000,
        /* 必须 ≥1：本值直接作为 spi 设备事务队列长度，0 会在 xQueueCreate
         * 断言崩溃（queue.c:573）；事务本身走同步 API，队列只占位 */
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_cfg, &s_io));

    /* 硬复位（Adafruit begin() 同款时序） */
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Rcmd1（initR 公共第一段） */
    lcd_cmd(CMD_SWRESET, NULL, 0, 150);
    lcd_cmd(CMD_SLPOUT, NULL, 0, 500);
    uint8_t rate[3] = { 0x01, 0x2C, 0x2D };
    lcd_cmd(CMD_FRMCTR1, rate, 3, 0);
    lcd_cmd(CMD_FRMCTR2, rate, 3, 0);
    uint8_t rate2[6] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D };
    lcd_cmd(CMD_FRMCTR3, rate2, 6, 0);
    uint8_t invctr = 0x07;
    lcd_cmd(CMD_INVCTR, &invctr, 1, 0);
    uint8_t pw1[3] = { 0xA2, 0x02, 0x84 };
    lcd_cmd(CMD_PWCTR1, pw1, 3, 0);
    uint8_t pw2 = 0xC5;
    lcd_cmd(CMD_PWCTR2, &pw2, 1, 0);
    uint8_t pw3[2] = { 0x0A, 0x00 };
    lcd_cmd(CMD_PWCTR3, pw3, 2, 0);
    uint8_t pw4[2] = { 0x8A, 0x2A };
    lcd_cmd(CMD_PWCTR4, pw4, 2, 0);
    uint8_t pw5[2] = { 0x8A, 0xEE };
    lcd_cmd(CMD_PWCTR5, pw5, 2, 0);
    uint8_t vmc1 = 0x0E;
    lcd_cmd(CMD_VMCTR1, &vmc1, 1, 0);
    lcd_cmd(CMD_INVOFF, NULL, 0, 0);
    uint8_t mad_c8 = 0xC8;
    lcd_cmd(CMD_MADCTL, &mad_c8, 1, 0);
    uint8_t colmod = 0x05; /* 16bit RGB565 */
    lcd_cmd(CMD_COLMOD, &colmod, 1, 0);

    /* Rcmd2green160x80：全屏默认窗（列 0..79 / 行 0..159）——本驱动每次画图
     * 都重设带偏移的窗，此处仅按已知好序列补发 */
    uint8_t full_caset[4] = { 0x00, 0x00, 0x00, 0x4F };
    lcd_cmd(CMD_CASET, full_caset, 4, 0);
    uint8_t full_raset[4] = { 0x00, 0x00, 0x00, 0x9F };
    lcd_cmd(CMD_RASET, full_raset, 4, 0);

    /* Rcmd3：gamma 校正 + 开屏 */
    uint8_t gamma_p[16] = { 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                            0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10 };
    lcd_cmd(CMD_GMCTRP1, gamma_p, 16, 0);
    uint8_t gamma_n[16] = { 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                            0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10 };
    lcd_cmd(CMD_GMCTRN1, gamma_n, 16, 0);
    lcd_cmd(CMD_NORON, NULL, 0, 10);
    lcd_cmd(CMD_DISPON, NULL, 0, 100);

    /* MINI160x80 的 RGB 滤色 MADCTL + rotation 3（MX|MV|RGB） */
    uint8_t mad_r3 = 0x60;
    lcd_cmd(CMD_MADCTL, &mad_r3, 1, 0);

    st7735_backlight(true);
    ESP_LOGI(TAG, "ST7735 160x80(rotation 3) 就绪：SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d",
             LCD_SCLK_GPIO, LCD_MOSI_GPIO, LCD_CS_GPIO, LCD_DC_GPIO,
             LCD_RST_GPIO, LCD_BL_GPIO);
    return ESP_OK;
}

void st7735_backlight(bool on)
{
    gpio_set_level(LCD_BL_GPIO, on ? 1 : 0);
}

void st7735_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ST7735_W) { w = ST7735_W - x; }
    if (y + h > ST7735_H) { h = ST7735_H - y; }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int i = 0; i < w; i++) {
        s_line[i] = color;
    }
    set_window(x, y, w, h);
    for (int r = 0; r < h; r++) {
        push_line(w);
    }
}

void st7735_fill_screen(uint16_t color)
{
    st7735_fill_rect(0, 0, ST7735_W, ST7735_H, color);
}

int st7735_text_width(const char *s, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    return (int)strlen(s) * 6 * scale - scale;
}

void st7735_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (scale < 1) {
        scale = 1;
    }
    int cw = 6 * scale;  /* 5px 字形 + 1px 字距 */
    int th = 7 * scale;
    if (x >= ST7735_W || y >= ST7735_H || !s || !*s) {
        return;
    }
    int w = (int)strlen(s) * cw - scale;
    if (x < 0) { /* 左裁剪整字符，避免负列索引 */
        int cut = (-x + cw - 1) / cw;
        s += cut;
        w -= cut * cw;
        x = 0;
    }
    if (x + w > ST7735_W) {
        w = ST7735_W - x;
    }
    if (w <= 0) {
        return;
    }
    for (int r = 0; r < th && y + r < ST7735_H; r++) {
        int sy = r / scale; /* 字形行 0..6（bit0 = 顶行） */
        for (int c = 0; c < w; c++) {
            int glyph_col = (c % cw);
            uint8_t bits = 0;
            if (glyph_col < 5) {
                char ch = s[c / cw];
                if (ch >= 0x20 && ch <= 0x7E) {
                    bits = kFont5x7[ch - 0x20][glyph_col];
                }
            }
            s_line[c] = ((bits >> sy) & 1) ? fg : bg;
        }
        set_window(x, y + r, w, 1);
        push_line(w);
    }
}
