/*
 * Copyright (c) 2026 minxie <laba22@163.com>
 * All rights reserved.
 */

/*
 * 无源蜂鸣器驱动库 (基于 LEDC PWM)
 * GPIO: 25 (默认)
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 预定义音符频率 (Hz) */
typedef enum {
    NOTE_C4  = 262,
    NOTE_CS4 = 277,
    NOTE_D4  = 294,
    NOTE_DS4 = 311,
    NOTE_E4  = 330,
    NOTE_F4  = 349,
    NOTE_FS4 = 370,
    NOTE_G4  = 392,
    NOTE_GS4 = 415,
    NOTE_A4  = 440,
    NOTE_AS4 = 466,
    NOTE_B4  = 494,
    NOTE_C5  = 523,
    NOTE_D5  = 587,
    NOTE_E5  = 659,
    NOTE_F5  = 698,
    NOTE_G5  = 784,
    NOTE_A5  = 880,
    NOTE_B5  = 988,
    NOTE_C6  = 1047,
    NOTE_REST = 0,   /**< 静音 (用于节拍间隔) */
} buzzer_note_t;

/** 蜂鸣器配置 */
typedef struct {
    int gpio_num;       /**< GPIO 引脚号, 默认 25 */
    int ledc_timer;     /**< LEDC 定时器号 (0-3), 默认 0 */
    int ledc_channel;   /**< LEDC 通道号 (0-7), 默认 0 */
} buzzer_config_t;

/** 默认配置宏 */
#define BUZZER_CONFIG_DEFAULT() { \
    .gpio_num = 25,               \
    .ledc_timer = 0,              \
    .ledc_channel = 0,            \
}

/** 乐谱中的一个音符 */
typedef struct {
    uint32_t freq_hz;       /**< 频率 (Hz), 0 = 静音 */
    uint32_t duration_ms;   /**< 持续时间 (ms) */
} buzzer_tone_t;

/**
 * @brief 初始化蜂鸣器
 * @param config  配置参数, 传 NULL 使用默认配置
 * @return ESP_OK 成功
 */
esp_err_t buzzer_init(const buzzer_config_t *config);

/**
 * @brief 反初始化, 释放资源
 */
esp_err_t buzzer_deinit(void);

/**
 * @brief 以指定频率发声
 * @param freq_hz  频率 (Hz), 范围 20~20000
 */
esp_err_t buzzer_tone(uint32_t freq_hz);

/**
 * @brief 发声指定时长后自动停止 (阻塞)
 * @param freq_hz      频率 (Hz)
 * @param duration_ms  持续时间 (ms)
 */
esp_err_t buzzer_tone_ms(uint32_t freq_hz, uint32_t duration_ms);

/**
 * @brief 停止发声
 */
esp_err_t buzzer_stop(void);

/**
 * @brief 设置音量 (PWM 占空比)
 * @param volume  0~100 (百分比), 默认 50
 */
esp_err_t buzzer_set_volume(uint8_t volume);

/**
 * @brief 发出短促哔声
 * @param count  哔声次数
 */
esp_err_t buzzer_beep(int count);

/**
 * @brief 播放一段旋律 (阻塞)
 * @param melody     音符数组
 * @param length     音符数量
 * @param pause_ms   音符之间的间隔 (ms), 建议 30~50
 */
esp_err_t buzzer_play_melody(const buzzer_tone_t *melody, size_t length, uint32_t pause_ms);

/**
 * @brief 播放开机提示音
 */
esp_err_t buzzer_play_startup(void);

/**
 * @brief 播放成功提示音
 */
esp_err_t buzzer_play_success(void);

/**
 * @brief 播放错误/警告提示音
 */
esp_err_t buzzer_play_error(void);

#ifdef __cplusplus
}
#endif
