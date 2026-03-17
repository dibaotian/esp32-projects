/*
 * 无源蜂鸣器驱动实现 (基于 LEDC PWM)
 */

#include <string.h>
#include "buzzer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "BUZZER"

#define BUZZER_FREQ_MIN   20
#define BUZZER_FREQ_MAX   20000
#define BUZZER_DUTY_RES   LEDC_TIMER_10_BIT   /* 0-1023 */
#define BUZZER_DUTY_MAX   1023
#define BUZZER_DEFAULT_VOL 50

static struct {
    bool           initialized;
    int            gpio_num;
    ledc_timer_t   timer;
    ledc_channel_t channel;
    uint8_t        volume;  /* 0~100 */
} s_buz = {
    .initialized = false,
    .volume = BUZZER_DEFAULT_VOL,
};

/* ---- 内部辅助 ---- */

static uint32_t volume_to_duty(uint8_t vol)
{
    /* 占空比 = volume% * 最大值 / 100, 限制最大 50% 避免过载 */
    uint32_t eff = (vol > 50) ? 50 : vol;
    return (BUZZER_DUTY_MAX * eff) / 100;
}

/* ---- 公开 API ---- */

esp_err_t buzzer_init(const buzzer_config_t *config)
{
    if (s_buz.initialized) {
        ESP_LOGW(TAG, "已初始化, 跳过");
        return ESP_OK;
    }

    buzzer_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        cfg = (buzzer_config_t)BUZZER_CONFIG_DEFAULT();
    }

    s_buz.gpio_num = cfg.gpio_num;
    s_buz.timer    = (ledc_timer_t)cfg.ledc_timer;
    s_buz.channel  = (ledc_channel_t)cfg.ledc_channel;

    /* 配置 LEDC 定时器 */
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = s_buz.timer,
        .duty_resolution = BUZZER_DUTY_RES,
        .freq_hz         = 1000,  /* 初始频率, 后续根据音调修改 */
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 定时器配置失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 配置 LEDC 通道 (duty = 0, 默认静音) */
    ledc_channel_config_t ch_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = s_buz.channel,
        .timer_sel  = s_buz.timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = s_buz.gpio_num,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 通道配置失败: %s", esp_err_to_name(err));
        return err;
    }

    s_buz.initialized = true;
    ESP_LOGI(TAG, "蜂鸣器初始化完成 (GPIO %d)", s_buz.gpio_num);
    return ESP_OK;
}

esp_err_t buzzer_deinit(void)
{
    if (!s_buz.initialized) {
        return ESP_OK;
    }
    ledc_stop(LEDC_LOW_SPEED_MODE, s_buz.channel, 0);
    s_buz.initialized = false;
    ESP_LOGI(TAG, "蜂鸣器已反初始化");
    return ESP_OK;
}

esp_err_t buzzer_tone(uint32_t freq_hz)
{
    if (!s_buz.initialized) return ESP_ERR_INVALID_STATE;
    if (freq_hz < BUZZER_FREQ_MIN || freq_hz > BUZZER_FREQ_MAX) {
        ESP_LOGE(TAG, "频率超出范围: %lu", (unsigned long)freq_hz);
        return ESP_ERR_INVALID_ARG;
    }

    /* 更新频率 */
    esp_err_t err = ledc_set_freq(LEDC_LOW_SPEED_MODE, s_buz.timer, freq_hz);
    if (err != ESP_OK) return err;

    /* 更新占空比 */
    uint32_t duty = volume_to_duty(s_buz.volume);
    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, s_buz.channel, duty);
    if (err != ESP_OK) return err;

    return ledc_update_duty(LEDC_LOW_SPEED_MODE, s_buz.channel);
}

esp_err_t buzzer_tone_ms(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz == 0 || freq_hz == NOTE_REST) {
        /* 静音：只等待 */
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return ESP_OK;
    }

    esp_err_t err = buzzer_tone(freq_hz);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return buzzer_stop();
}

esp_err_t buzzer_stop(void)
{
    if (!s_buz.initialized) return ESP_ERR_INVALID_STATE;

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, s_buz.channel, 0);
    if (err != ESP_OK) return err;
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, s_buz.channel);
}

esp_err_t buzzer_set_volume(uint8_t volume)
{
    if (!s_buz.initialized) return ESP_ERR_INVALID_STATE;
    s_buz.volume = (volume > 100) ? 100 : volume;
    ESP_LOGI(TAG, "音量设置为 %d%%", s_buz.volume);
    return ESP_OK;
}

esp_err_t buzzer_beep(int count)
{
    if (!s_buz.initialized) return ESP_ERR_INVALID_STATE;

    for (int i = 0; i < count; i++) {
        buzzer_tone(NOTE_A5);
        vTaskDelay(pdMS_TO_TICKS(100));
        buzzer_stop();
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
    return ESP_OK;
}

esp_err_t buzzer_play_melody(const buzzer_tone_t *melody, size_t length, uint32_t pause_ms)
{
    if (!s_buz.initialized) return ESP_ERR_INVALID_STATE;
    if (!melody || length == 0) return ESP_ERR_INVALID_ARG;

    for (size_t i = 0; i < length; i++) {
        buzzer_tone_ms(melody[i].freq_hz, melody[i].duration_ms);
        if (pause_ms > 0 && i < length - 1) {
            vTaskDelay(pdMS_TO_TICKS(pause_ms));
        }
    }
    return ESP_OK;
}

esp_err_t buzzer_play_startup(void)
{
    static const buzzer_tone_t melody[] = {
        { NOTE_C5,  120 },
        { NOTE_E5,  120 },
        { NOTE_G5,  120 },
        { NOTE_C6,  250 },
    };
    return buzzer_play_melody(melody, sizeof(melody) / sizeof(melody[0]), 30);
}

esp_err_t buzzer_play_success(void)
{
    static const buzzer_tone_t melody[] = {
        { NOTE_C5,  100 },
        { NOTE_E5,  100 },
        { NOTE_G5,  200 },
    };
    return buzzer_play_melody(melody, sizeof(melody) / sizeof(melody[0]), 30);
}

esp_err_t buzzer_play_error(void)
{
    static const buzzer_tone_t melody[] = {
        { NOTE_A4,  200 },
        { NOTE_REST, 80 },
        { NOTE_A4,  200 },
        { NOTE_REST, 80 },
        { NOTE_A4,  400 },
    };
    return buzzer_play_melody(melody, sizeof(melody) / sizeof(melody[0]), 0);
}
