/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * TM1637 四位七段数码管驱动 (ESP-IDF)
 *
 * 两线协议 (CLK + DIO), 类似 I2C 但非标准
 * 默认引脚: CLK=GPIO16, DIO=GPIO17
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** TM1637 句柄 */
typedef struct tm1637_dev *tm1637_handle_t;

/** TM1637 配置 */
typedef struct {
    int clk_gpio;       /**< 时钟引脚, 默认 16 */
    int dio_gpio;       /**< 数据引脚, 默认 17 */
    uint8_t brightness; /**< 亮度 0~7 */
} tm1637_config_t;

/** 默认配置宏 */
#define TM1637_CONFIG_DEFAULT() { \
    .clk_gpio   = 16,            \
    .dio_gpio   = 17,            \
    .brightness = 4,             \
}

/**
 * @brief 初始化 TM1637
 * @param config 配置参数
 * @param[out] handle 返回句柄
 */
esp_err_t tm1637_init(const tm1637_config_t *config, tm1637_handle_t *handle);

/**
 * @brief 释放 TM1637
 */
esp_err_t tm1637_deinit(tm1637_handle_t handle);

/**
 * @brief 设置亮度
 * @param brightness 0~7
 */
esp_err_t tm1637_set_brightness(tm1637_handle_t handle, uint8_t brightness);

/**
 * @brief 显示 4 位十进制数 (0000~9999)
 * @param num 数值
 * @param leading_zero 是否显示前导零
 */
esp_err_t tm1637_show_number(tm1637_handle_t handle, int num, bool leading_zero);

/**
 * @brief 显示 4 个字符的字符串
 *        支持: 0-9, A-F, a-f, '-', ' ', '_'
 */
esp_err_t tm1637_show_string(tm1637_handle_t handle, const char *str);

/**
 * @brief 直接设置 4 个段码
 * @param segments 4 字节段码数组
 */
esp_err_t tm1637_show_raw(tm1637_handle_t handle, const uint8_t segments[4]);

/**
 * @brief 清空显示
 */
esp_err_t tm1637_clear(tm1637_handle_t handle);

/**
 * @brief 设置/清除冒号 (第二位的小数点)
 */
esp_err_t tm1637_set_colon(tm1637_handle_t handle, bool on);

#ifdef __cplusplus
}
#endif
