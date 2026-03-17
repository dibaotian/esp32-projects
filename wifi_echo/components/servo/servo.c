/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * 舵机驱动实现 (基于 LEDC PWM, 50Hz)
 *
 * LEDC 配置:
 *   - 频率: 50Hz (周期 20ms)
 *   - 分辨率: 14-bit (16384 级, 每级 ≈ 1.22μs)
 *   - 脉宽范围: 500~2500μs → 约 1639 级精度
 */

#include <stdlib.h>
#include <math.h>
#include "servo.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "SERVO"

#define SERVO_PWM_FREQ     50         /* 50Hz = 20ms 周期 */
#define SERVO_DUTY_RES     LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX     16383      /* 2^14 - 1 */
#define SERVO_PERIOD_US    20000      /* 20ms */
#define SERVO_STEP_MS      10         /* 平滑移动步进间隔 */

/** 内部结构体 */
struct servo_handle {
    int            gpio_num;
    ledc_timer_t   timer;
    ledc_channel_t channel;
    uint32_t       min_pulse_us;
    uint32_t       max_pulse_us;
    uint32_t       max_angle;
    float          current_angle;
    bool           initialized;
};

/* ---- 内部辅助 ---- */

static uint32_t pulse_us_to_duty(uint32_t pulse_us)
{
    /* duty = pulse_us / period_us * duty_max */
    return (uint32_t)((uint64_t)pulse_us * SERVO_DUTY_MAX / SERVO_PERIOD_US);
}

static uint32_t angle_to_pulse_us(servo_handle_t s, float angle)
{
    if (angle < 0) angle = 0;
    if (angle > s->max_angle) angle = (float)s->max_angle;
    return s->min_pulse_us +
           (uint32_t)((angle / s->max_angle) * (s->max_pulse_us - s->min_pulse_us));
}

static esp_err_t set_duty(servo_handle_t s, uint32_t duty)
{
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, s->channel, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, s->channel);
}

/* ---- 公开 API ---- */

esp_err_t servo_init(const servo_config_t *config, servo_handle_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    servo_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        cfg = (servo_config_t)SERVO_CONFIG_DEFAULT();
    }

    /* 参数校验 */
    if (cfg.min_pulse_us >= cfg.max_pulse_us) {
        ESP_LOGE(TAG, "min_pulse_us (%lu) >= max_pulse_us (%lu)",
                 (unsigned long)cfg.min_pulse_us, (unsigned long)cfg.max_pulse_us);
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg.max_angle == 0) {
        ESP_LOGE(TAG, "max_angle 不能为 0");
        return ESP_ERR_INVALID_ARG;
    }

    struct servo_handle *s = calloc(1, sizeof(struct servo_handle));
    if (!s) return ESP_ERR_NO_MEM;

    s->gpio_num     = cfg.gpio_num;
    s->timer        = (ledc_timer_t)cfg.ledc_timer;
    s->channel      = (ledc_channel_t)cfg.ledc_channel;
    s->min_pulse_us = cfg.min_pulse_us;
    s->max_pulse_us = cfg.max_pulse_us;
    s->max_angle    = cfg.max_angle;
    s->current_angle = 0;

    /* 配置 LEDC 定时器: 50Hz, 14-bit */
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = s->timer,
        .duty_resolution = SERVO_DUTY_RES,
        .freq_hz         = SERVO_PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 定时器配置失败: %s", esp_err_to_name(err));
        free(s);
        return err;
    }

    /* 配置 LEDC 通道 (初始 duty=0, 不输出) */
    ledc_channel_config_t ch_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = s->channel,
        .timer_sel  = s->timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = s->gpio_num,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 通道配置失败: %s", esp_err_to_name(err));
        free(s);
        return err;
    }

    s->initialized = true;
    *handle = s;

    ESP_LOGI(TAG, "舵机初始化完成 (GPIO %d, %lu~%luμs, 0~%lu°)",
             s->gpio_num,
             (unsigned long)s->min_pulse_us,
             (unsigned long)s->max_pulse_us,
             (unsigned long)s->max_angle);
    return ESP_OK;
}

esp_err_t servo_deinit(servo_handle_t handle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    ledc_stop(LEDC_LOW_SPEED_MODE, handle->channel, 0);
    handle->initialized = false;
    ESP_LOGI(TAG, "舵机已反初始化 (GPIO %d)", handle->gpio_num);
    free(handle);
    return ESP_OK;
}

esp_err_t servo_set_angle(servo_handle_t handle, float angle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    if (angle < 0) angle = 0;
    if (angle > handle->max_angle) angle = (float)handle->max_angle;

    uint32_t pulse_us = angle_to_pulse_us(handle, angle);
    uint32_t duty = pulse_us_to_duty(pulse_us);

    esp_err_t err = set_duty(handle, duty);
    if (err != ESP_OK) return err;

    handle->current_angle = angle;
    ESP_LOGD(TAG, "角度 %.1f° -> 脉宽 %luμs -> duty %lu",
             angle, (unsigned long)pulse_us, (unsigned long)duty);
    return ESP_OK;
}

float servo_get_angle(servo_handle_t handle)
{
    if (!handle || !handle->initialized) return -1;
    return handle->current_angle;
}

esp_err_t servo_set_pulse_width(servo_handle_t handle, uint32_t pulse_us)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    if (pulse_us < handle->min_pulse_us) pulse_us = handle->min_pulse_us;
    if (pulse_us > handle->max_pulse_us) pulse_us = handle->max_pulse_us;

    uint32_t duty = pulse_us_to_duty(pulse_us);
    esp_err_t err = set_duty(handle, duty);
    if (err != ESP_OK) return err;

    /* 反算角度 */
    handle->current_angle = (float)(pulse_us - handle->min_pulse_us) /
                            (handle->max_pulse_us - handle->min_pulse_us) *
                            handle->max_angle;
    return ESP_OK;
}

esp_err_t servo_smooth_move(servo_handle_t handle, float angle, float speed)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (speed <= 0) return ESP_ERR_INVALID_ARG;

    if (angle < 0) angle = 0;
    if (angle > handle->max_angle) angle = (float)handle->max_angle;

    float current = handle->current_angle;
    float diff = angle - current;
    if (fabsf(diff) < 0.5f) {
        return servo_set_angle(handle, angle);
    }

    /* 每步角度 = speed * step_interval */
    float step_deg = speed * SERVO_STEP_MS / 1000.0f;
    float direction = (diff > 0) ? 1.0f : -1.0f;
    step_deg *= direction;

    while (1) {
        current += step_deg;

        /* 判断是否到达或超过目标 */
        if ((direction > 0 && current >= angle) ||
            (direction < 0 && current <= angle)) {
            return servo_set_angle(handle, angle);
        }

        esp_err_t err = servo_set_angle(handle, current);
        if (err != ESP_OK) return err;

        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_MS));
    }
}

esp_err_t servo_sweep(servo_handle_t handle, float start, float end,
                      float speed, int count)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;
    if (speed <= 0) return ESP_ERR_INVALID_ARG;

    /* 先移动到起始位置 */
    esp_err_t err = servo_smooth_move(handle, start, speed);
    if (err != ESP_OK) return err;

    if (count == 0) {
        /* 单程: start -> end */
        return servo_smooth_move(handle, end, speed);
    }

    for (int i = 0; i < count; i++) {
        err = servo_smooth_move(handle, end, speed);
        if (err != ESP_OK) return err;
        err = servo_smooth_move(handle, start, speed);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t servo_stop(servo_handle_t handle)
{
    if (!handle || !handle->initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t err = set_duty(handle, 0);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "舵机 PWM 已停止 (力矩释放)");
    return ESP_OK;
}
