/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * Grove LCD RGB Backlight 驱动实现
 *
 * LCD 控制器 AIP31068 (HD44780 兼容):
 *   I2C 地址 0x3E, 指令/数据通过 I2C 写入
 *   指令: 0x80 + cmd_byte
 *   数据: 0x40 + data_byte
 *
 * RGB 背光 PCA9632:
 *   I2C 地址 0x62, 4 通道 LED PWM 驱动
 *   REG2=R, REG3=G, REG4=B (PWM 占空比)
 */

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include "grove_lcd.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GROVE_LCD";

/* I2C 地址 */
#define LCD_ADDR    0x3E
#define RGB_ADDR    0x62

/* HD44780 指令 */
#define LCD_CMD_CLEAR           0x01
#define LCD_CMD_HOME            0x02
#define LCD_CMD_ENTRY_MODE      0x04
#define LCD_CMD_DISPLAY_CTRL    0x08
#define LCD_CMD_CURSOR_SHIFT    0x10
#define LCD_CMD_FUNCTION_SET    0x20
#define LCD_CMD_SET_CGRAM       0x40
#define LCD_CMD_SET_DDRAM       0x80

/* Entry mode 标志 */
#define LCD_ENTRY_INCREMENT     0x02
#define LCD_ENTRY_SHIFT         0x01

/* Display control 标志 */
#define LCD_DISPLAY_ON          0x04
#define LCD_CURSOR_ON           0x02
#define LCD_BLINK_ON            0x01

/* Function set 标志 */
#define LCD_2LINE               0x08
#define LCD_5x8DOTS             0x00

/* PCA9632 寄存器 */
#define PCA_REG_MODE1       0x00
#define PCA_REG_MODE2       0x01
#define PCA_REG_PWM0        0x02  /* B */
#define PCA_REG_PWM1        0x03  /* G */
#define PCA_REG_PWM2        0x04  /* R */
#define PCA_REG_GRPPWM      0x06
#define PCA_REG_GRPFREQ     0x07
#define PCA_REG_LEDOUT      0x08

/* I2C 超时 */
#define I2C_TIMEOUT_MS  100

struct grove_lcd_dev {
    int i2c_port;
    uint8_t display_ctrl;   /* 当前 display control 状态 */
    uint8_t entry_mode;     /* 当前 entry mode 状态 */
};

/* ---- I2C 底层 ---- */

static esp_err_t lcd_write_cmd(grove_lcd_handle_t dev, uint8_t cmd)
{
    uint8_t buf[2] = { 0x80, cmd };
    return i2c_master_write_to_device(dev->i2c_port, LCD_ADDR, buf, 2,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t lcd_write_data(grove_lcd_handle_t dev, uint8_t data)
{
    uint8_t buf[2] = { 0x40, data };
    return i2c_master_write_to_device(dev->i2c_port, LCD_ADDR, buf, 2,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t rgb_write_reg(grove_lcd_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(dev->i2c_port, RGB_ADDR, buf, 2,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/* ---- 公共 API: 初始化/释放 ---- */

esp_err_t grove_lcd_init(const grove_lcd_config_t *config, grove_lcd_handle_t *handle)
{
    if (!config || !handle) return ESP_ERR_INVALID_ARG;

    /* 配置 I2C */
    i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = config->sda_gpio,
        .scl_io_num       = config->scl_gpio,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = config->i2c_freq,
    };
    esp_err_t ret = i2c_param_config(config->i2c_port, &i2c_conf);
    if (ret != ESP_OK) return ret;

    ret = i2c_driver_install(config->i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) return ret;

    grove_lcd_handle_t dev = calloc(1, sizeof(struct grove_lcd_dev));
    if (!dev) {
        i2c_driver_delete(config->i2c_port);
        return ESP_ERR_NO_MEM;
    }
    dev->i2c_port = config->i2c_port;

    /* LCD 初始化序列 (HD44780)
     * 注: vTaskDelay(pdMS_TO_TICKS(N)) at 100Hz tick = 0 when N<10,
     *     所以使用 esp_rom_delay_us() 精确忙等 */
    vTaskDelay(pdMS_TO_TICKS(50));  /* 等待 LCD 上电稳定 (50ms → 5 ticks OK) */

    /* Function set: 2行, 5x8点阵 */
    lcd_write_cmd(dev, LCD_CMD_FUNCTION_SET | LCD_2LINE | LCD_5x8DOTS);
    esp_rom_delay_us(5000);

    /* Display on, cursor off, blink off */
    dev->display_ctrl = LCD_DISPLAY_ON;
    lcd_write_cmd(dev, LCD_CMD_DISPLAY_CTRL | dev->display_ctrl);
    esp_rom_delay_us(5000);

    /* Clear */
    lcd_write_cmd(dev, LCD_CMD_CLEAR);
    esp_rom_delay_us(2000);

    /* Entry mode: increment, no shift */
    dev->entry_mode = LCD_ENTRY_INCREMENT;
    lcd_write_cmd(dev, LCD_CMD_ENTRY_MODE | dev->entry_mode);
    esp_rom_delay_us(5000);

    /* RGB 背光初始化 (PCA9632) */
    rgb_write_reg(dev, PCA_REG_MODE1, 0x00);   /* Normal mode */
    rgb_write_reg(dev, PCA_REG_MODE2, 0x20);   /* DMBLNK = group dimming */
    rgb_write_reg(dev, PCA_REG_LEDOUT, 0xAA);  /* 全部由 PWM 控制 */
    rgb_write_reg(dev, PCA_REG_GRPPWM, 0xFF);  /* Group PWM = 最大 */
    rgb_write_reg(dev, PCA_REG_GRPFREQ, 0x00);

    /* 默认白色背光 */
    rgb_write_reg(dev, PCA_REG_PWM0, 0xFF);
    rgb_write_reg(dev, PCA_REG_PWM1, 0xFF);
    rgb_write_reg(dev, PCA_REG_PWM2, 0xFF);

    *handle = dev;
    ESP_LOGI(TAG, "Grove LCD RGB 初始化完成 (SDA=%d, SCL=%d, I2C%d)",
             config->sda_gpio, config->scl_gpio, config->i2c_port);
    return ESP_OK;
}

esp_err_t grove_lcd_deinit(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    grove_lcd_clear(handle);
    grove_lcd_backlight_off(handle);
    i2c_driver_delete(handle->i2c_port);
    free(handle);
    return ESP_OK;
}

/* ---- LCD 显示控制 ---- */

esp_err_t grove_lcd_clear(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = lcd_write_cmd(handle, LCD_CMD_CLEAR);
    esp_rom_delay_us(2000);  /* HD44780 clear 需要 1.52ms */
    return ret;
}

esp_err_t grove_lcd_home(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = lcd_write_cmd(handle, LCD_CMD_HOME);
    esp_rom_delay_us(2000);  /* HD44780 home 需要 1.52ms */
    return ret;
}

esp_err_t grove_lcd_set_cursor(grove_lcd_handle_t handle, uint8_t col, uint8_t row)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (col > 15) col = 15;
    if (row > 1) row = 1;
    uint8_t addr = col + (row == 0 ? 0x00 : 0x40);
    return lcd_write_cmd(handle, LCD_CMD_SET_DDRAM | addr);
}

esp_err_t grove_lcd_display_on(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->display_ctrl |= LCD_DISPLAY_ON;
    return lcd_write_cmd(handle, LCD_CMD_DISPLAY_CTRL | handle->display_ctrl);
}

esp_err_t grove_lcd_display_off(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->display_ctrl &= ~LCD_DISPLAY_ON;
    return lcd_write_cmd(handle, LCD_CMD_DISPLAY_CTRL | handle->display_ctrl);
}

esp_err_t grove_lcd_cursor_on(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->display_ctrl |= LCD_CURSOR_ON;
    return lcd_write_cmd(handle, LCD_CMD_DISPLAY_CTRL | handle->display_ctrl);
}

esp_err_t grove_lcd_cursor_off(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->display_ctrl &= ~LCD_CURSOR_ON;
    return lcd_write_cmd(handle, LCD_CMD_DISPLAY_CTRL | handle->display_ctrl);
}

esp_err_t grove_lcd_blink_on(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->display_ctrl |= LCD_BLINK_ON;
    return lcd_write_cmd(handle, LCD_CMD_DISPLAY_CTRL | handle->display_ctrl);
}

esp_err_t grove_lcd_blink_off(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->display_ctrl &= ~LCD_BLINK_ON;
    return lcd_write_cmd(handle, LCD_CMD_DISPLAY_CTRL | handle->display_ctrl);
}

esp_err_t grove_lcd_write_char(grove_lcd_handle_t handle, char c)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return lcd_write_data(handle, (uint8_t)c);
}

esp_err_t grove_lcd_print(grove_lcd_handle_t handle, const char *str)
{
    if (!handle || !str) return ESP_ERR_INVALID_ARG;
    while (*str) {
        lcd_write_data(handle, (uint8_t)*str++);
    }
    return ESP_OK;
}

esp_err_t grove_lcd_printf(grove_lcd_handle_t handle, const char *fmt, ...)
{
    if (!handle || !fmt) return ESP_ERR_INVALID_ARG;
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return grove_lcd_print(handle, buf);
}

esp_err_t grove_lcd_create_char(grove_lcd_handle_t handle, uint8_t location, const uint8_t charmap[8])
{
    if (!handle || !charmap) return ESP_ERR_INVALID_ARG;
    location &= 0x07;  /* 0~7 */
    lcd_write_cmd(handle, LCD_CMD_SET_CGRAM | (location << 3));
    for (int i = 0; i < 8; i++) {
        lcd_write_data(handle, charmap[i]);
    }
    return ESP_OK;
}

esp_err_t grove_lcd_scroll_left(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return lcd_write_cmd(handle, LCD_CMD_CURSOR_SHIFT | 0x18);
}

esp_err_t grove_lcd_scroll_right(grove_lcd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return lcd_write_cmd(handle, LCD_CMD_CURSOR_SHIFT | 0x1C);
}

/* ---- RGB 背光 ---- */

esp_err_t grove_lcd_set_rgb(grove_lcd_handle_t handle, uint8_t r, uint8_t g, uint8_t b)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    rgb_write_reg(handle, PCA_REG_PWM0, b);
    rgb_write_reg(handle, PCA_REG_PWM1, g);
    rgb_write_reg(handle, PCA_REG_PWM2, r);
    return ESP_OK;
}

esp_err_t grove_lcd_backlight_off(grove_lcd_handle_t handle)
{
    return grove_lcd_set_rgb(handle, 0, 0, 0);
}

esp_err_t grove_lcd_backlight_on(grove_lcd_handle_t handle)
{
    return grove_lcd_set_rgb(handle, 255, 255, 255);
}
