/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * 舵机驱动库 (基于 LEDC PWM)
 * 支持多路舵机独立控制
 * 默认 GPIO: 27
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 舵机句柄 (支持多路) */
typedef struct servo_handle *servo_handle_t;

/** 舵机配置 */
typedef struct {
    int gpio_num;            /**< PWM 输出引脚, 默认 27 */
    int ledc_timer;          /**< LEDC 定时器号 (0-3), 默认 1 */
    int ledc_channel;        /**< LEDC 通道号 (0-7), 默认 1 */
    uint32_t min_pulse_us;   /**< 最小脉宽 (μs), 对应 0°, 默认 500 */
    uint32_t max_pulse_us;   /**< 最大脉宽 (μs), 对应最大角度, 默认 2500 */
    uint32_t max_angle;      /**< 最大角度 (°), 默认 180 */
} servo_config_t;

/** 默认配置宏 (GPIO 27, 0~180°, 500~2500μs) */
#define SERVO_CONFIG_DEFAULT() {   \
    .gpio_num     = 27,            \
    .ledc_timer   = 1,             \
    .ledc_channel = 1,             \
    .min_pulse_us = 500,           \
    .max_pulse_us = 2500,          \
    .max_angle    = 180,           \
}

/**
 * @brief 初始化舵机
 * @param config     配置参数, 传 NULL 使用默认配置
 * @param[out] handle  返回舵机句柄
 * @return ESP_OK 成功
 */
esp_err_t servo_init(const servo_config_t *config, servo_handle_t *handle);

/**
 * @brief 反初始化, 释放资源
 * @param handle  舵机句柄
 */
esp_err_t servo_deinit(servo_handle_t handle);

/**
 * @brief 设置舵机角度
 * @param handle  舵机句柄
 * @param angle   目标角度 (0 ~ max_angle)
 */
esp_err_t servo_set_angle(servo_handle_t handle, float angle);

/**
 * @brief 获取当前角度
 * @param handle  舵机句柄
 * @return 当前角度 (°)
 */
float servo_get_angle(servo_handle_t handle);

/**
 * @brief 设置原始脉宽
 * @param handle    舵机句柄
 * @param pulse_us  脉宽 (μs), 范围 min_pulse_us ~ max_pulse_us
 */
esp_err_t servo_set_pulse_width(servo_handle_t handle, uint32_t pulse_us);

/**
 * @brief 平滑移动到目标角度 (阻塞)
 * @param handle    舵机句柄
 * @param angle     目标角度
 * @param speed     移动速度 (°/s), 如 60 表示每秒 60°
 */
esp_err_t servo_smooth_move(servo_handle_t handle, float angle, float speed);

/**
 * @brief 在两个角度之间往复扫描 (阻塞)
 * @param handle    舵机句柄
 * @param start     起始角度
 * @param end       终止角度
 * @param speed     移动速度 (°/s)
 * @param count     往复次数, 0 = 单程
 */
esp_err_t servo_sweep(servo_handle_t handle, float start, float end, float speed, int count);

/**
 * @brief 停止 PWM 输出 (舵机释放力矩)
 * @param handle  舵机句柄
 */
esp_err_t servo_stop(servo_handle_t handle);

#ifdef __cplusplus
}
#endif
