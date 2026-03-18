/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * Grove LCD RGB Backlight 驱动 (ESP-IDF)
 *
 * I2C 双地址:
 *   LCD 控制器 (AIP31068): 0x3E — HD44780 兼容, 16x2 字符
 *   RGB 背光 (PCA9632):    0x62 — RGB LED PWM 控制
 *
 * 默认引脚: SDA=GPIO18, SCL=GPIO23
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** LCD RGB 句柄 */
typedef struct grove_lcd_dev *grove_lcd_handle_t;

/** 配置 */
typedef struct {
    int sda_gpio;       /**< I2C SDA 引脚, 默认 18 */
    int scl_gpio;       /**< I2C SCL 引脚, 默认 23 */
    int i2c_port;       /**< I2C 端口号, 默认 0 */
    uint32_t i2c_freq;  /**< I2C 频率, 默认 100kHz */
} grove_lcd_config_t;

/** 默认配置宏 */
#define GROVE_LCD_CONFIG_DEFAULT() { \
    .sda_gpio = 18,                 \
    .scl_gpio = 23,                 \
    .i2c_port = 0,                  \
    .i2c_freq = 100000,             \
}

/* ---- 初始化/释放 ---- */

esp_err_t grove_lcd_init(const grove_lcd_config_t *config, grove_lcd_handle_t *handle);
esp_err_t grove_lcd_deinit(grove_lcd_handle_t handle);

/* ---- LCD 显示控制 ---- */

/** 清屏 */
esp_err_t grove_lcd_clear(grove_lcd_handle_t handle);

/** 光标回到 (0,0) */
esp_err_t grove_lcd_home(grove_lcd_handle_t handle);

/** 设置光标位置 (col: 0~15, row: 0~1) */
esp_err_t grove_lcd_set_cursor(grove_lcd_handle_t handle, uint8_t col, uint8_t row);

/** 显示开/关 */
esp_err_t grove_lcd_display_on(grove_lcd_handle_t handle);
esp_err_t grove_lcd_display_off(grove_lcd_handle_t handle);

/** 光标显示开/关 */
esp_err_t grove_lcd_cursor_on(grove_lcd_handle_t handle);
esp_err_t grove_lcd_cursor_off(grove_lcd_handle_t handle);

/** 光标闪烁开/关 */
esp_err_t grove_lcd_blink_on(grove_lcd_handle_t handle);
esp_err_t grove_lcd_blink_off(grove_lcd_handle_t handle);

/** 打印字符串 (从当前光标位置) */
esp_err_t grove_lcd_print(grove_lcd_handle_t handle, const char *str);

/** 打印格式化字符串 */
esp_err_t grove_lcd_printf(grove_lcd_handle_t handle, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/** 写入单个字符 */
esp_err_t grove_lcd_write_char(grove_lcd_handle_t handle, char c);

/** 创建自定义字符 (location: 0~7, charmap: 8 字节) */
esp_err_t grove_lcd_create_char(grove_lcd_handle_t handle, uint8_t location, const uint8_t charmap[8]);

/** 滚动显示 */
esp_err_t grove_lcd_scroll_left(grove_lcd_handle_t handle);
esp_err_t grove_lcd_scroll_right(grove_lcd_handle_t handle);

/* ---- RGB 背光控制 ---- */

/** 设置 RGB 背光颜色 */
esp_err_t grove_lcd_set_rgb(grove_lcd_handle_t handle, uint8_t r, uint8_t g, uint8_t b);

/** 关闭背光 */
esp_err_t grove_lcd_backlight_off(grove_lcd_handle_t handle);

/** 开启背光 (白色) */
esp_err_t grove_lcd_backlight_on(grove_lcd_handle_t handle);

#ifdef __cplusplus
}
#endif
