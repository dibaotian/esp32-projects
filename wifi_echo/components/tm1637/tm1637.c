/*
 * TM1637 四位七段数码管驱动实现
 *
 * TM1637 使用类 I2C 的两线通信协议:
 *   - Start: CLK高电平时 DIO 拉低
 *   - Stop:  CLK高电平时 DIO 拉高
 *   - 数据 LSB 先发, CLK 上升沿锁存
 *   - 每 8bit 后有 ACK (DIO 被 TM1637 拉低)
 */

#include <string.h>
#include <stdlib.h>
#include "tm1637.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "rom/ets_sys.h"   /* ets_delay_us */

static const char *TAG = "TM1637";

/* TM1637 命令 */
#define TM1637_CMD_DATA    0x40  /* 数据命令: 写显示寄存器, 地址自增 */
#define TM1637_CMD_ADDR    0xC0  /* 地址命令: 起始地址 C0H */
#define TM1637_CMD_CTRL    0x80  /* 显示控制命令 */
#define TM1637_CTRL_ON     0x08  /* 显示开 */

/* 位延时 (微秒) - 加大延时确保 TM1637 稳定通信 */
#define BIT_DELAY_US  50

/* 段码表: 0~9, A~F */
static const uint8_t SEGMENT_MAP[] = {
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F, /* 9 */
    0x77, /* A */
    0x7C, /* b */
    0x39, /* C */
    0x5E, /* d */
    0x79, /* E */
    0x71, /* F */
};

struct tm1637_dev {
    int clk_gpio;
    int dio_gpio;
    uint8_t brightness;
    uint8_t segments[4];
    bool colon;
};

/* ---------- 底层位操作 ---------- */

static void tm1637_clk_high(tm1637_handle_t dev)
{
    gpio_set_level(dev->clk_gpio, 1);
    ets_delay_us(BIT_DELAY_US);
}

static void tm1637_clk_low(tm1637_handle_t dev)
{
    gpio_set_level(dev->clk_gpio, 0);
    ets_delay_us(BIT_DELAY_US);
}

static void tm1637_dio_high(tm1637_handle_t dev)
{
    gpio_set_level(dev->dio_gpio, 1);
    ets_delay_us(BIT_DELAY_US);
}

static void tm1637_dio_low(tm1637_handle_t dev)
{
    gpio_set_level(dev->dio_gpio, 0);
    ets_delay_us(BIT_DELAY_US);
}

static void tm1637_start(tm1637_handle_t dev)
{
    tm1637_dio_high(dev);
    tm1637_clk_high(dev);
    tm1637_dio_low(dev);   /* CLK高时 DIO下降沿 = START */
    tm1637_clk_low(dev);
}

static void tm1637_stop(tm1637_handle_t dev)
{
    tm1637_clk_low(dev);
    tm1637_dio_low(dev);
    tm1637_clk_high(dev);
    tm1637_dio_high(dev);  /* CLK高时 DIO上升沿 = STOP */
}

static void tm1637_write_byte(tm1637_handle_t dev, uint8_t data)
{
    for (int i = 0; i < 8; i++) {
        tm1637_clk_low(dev);
        if (data & (1 << i)) {
            tm1637_dio_high(dev);
        } else {
            tm1637_dio_low(dev);
        }
        tm1637_clk_high(dev);
    }
    /* ACK: 第 9 个时钟, TM1637 拉低 DIO */
    tm1637_clk_low(dev);
    tm1637_dio_high(dev);  /* 释放 DIO */
    tm1637_clk_high(dev);
    ets_delay_us(BIT_DELAY_US);
    /* 不检查 ACK, 直接继续 */
    tm1637_clk_low(dev);
}

/* ---------- 刷新显示 ---------- */

static void tm1637_refresh(tm1637_handle_t dev)
{
    uint8_t segs[4];
    memcpy(segs, dev->segments, 4);

    /* 冒号: 第二位 (index=1) 的 bit7 (0x80) */
    if (dev->colon) {
        segs[1] |= 0x80;
    }

    /* 1) 数据命令: 自动地址递增 */
    tm1637_start(dev);
    tm1637_write_byte(dev, TM1637_CMD_DATA);
    tm1637_stop(dev);

    /* 2) 地址命令 + 4字节段码 */
    tm1637_start(dev);
    tm1637_write_byte(dev, TM1637_CMD_ADDR);
    for (int i = 0; i < 4; i++) {
        tm1637_write_byte(dev, segs[i]);
    }
    tm1637_stop(dev);

    /* 3) 显示控制: 开显示 + 亮度 */
    tm1637_start(dev);
    tm1637_write_byte(dev, TM1637_CMD_CTRL | TM1637_CTRL_ON | (dev->brightness & 0x07));
    tm1637_stop(dev);
}

/* ---------- 公共 API ---------- */

esp_err_t tm1637_init(const tm1637_config_t *config, tm1637_handle_t *handle)
{
    if (!config || !handle) return ESP_ERR_INVALID_ARG;

    tm1637_handle_t dev = calloc(1, sizeof(struct tm1637_dev));
    if (!dev) return ESP_ERR_NO_MEM;

    dev->clk_gpio   = config->clk_gpio;
    dev->dio_gpio   = config->dio_gpio;
    dev->brightness = config->brightness & 0x07;

    /* 配置 CLK GPIO: 推挽输出 */
    gpio_config_t clk_conf = {
        .pin_bit_mask = (1ULL << dev->clk_gpio),
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&clk_conf);

    /* 配置 DIO GPIO: 推挽输出 + 上拉 */
    gpio_config_t dio_conf = {
        .pin_bit_mask = (1ULL << dev->dio_gpio),
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&dio_conf);

    gpio_set_level(dev->clk_gpio, 1);
    gpio_set_level(dev->dio_gpio, 1);
    ets_delay_us(1000);  /* 等待 TM1637 稳定 */

    /* 先发一次显示开命令, 确保 TM1637 被唤醒 */
    tm1637_start(dev);
    tm1637_write_byte(dev, TM1637_CMD_CTRL | TM1637_CTRL_ON | 0x07);
    tm1637_stop(dev);
    ets_delay_us(500);

    /* 清空显示 */
    memset(dev->segments, 0, 4);
    tm1637_refresh(dev);

    *handle = dev;
    ESP_LOGI(TAG, "TM1637 初始化完成 (CLK=%d, DIO=%d, 亮度=%d)",
             dev->clk_gpio, dev->dio_gpio, dev->brightness);
    return ESP_OK;
}

esp_err_t tm1637_deinit(tm1637_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    tm1637_clear(handle);
    gpio_reset_pin(handle->clk_gpio);
    gpio_reset_pin(handle->dio_gpio);
    free(handle);
    return ESP_OK;
}

esp_err_t tm1637_set_brightness(tm1637_handle_t handle, uint8_t brightness)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->brightness = brightness & 0x07;
    tm1637_refresh(handle);
    return ESP_OK;
}

static uint8_t char_to_segment(char c)
{
    if (c >= '0' && c <= '9') return SEGMENT_MAP[c - '0'];
    if (c >= 'A' && c <= 'F') return SEGMENT_MAP[c - 'A' + 10];
    if (c >= 'a' && c <= 'f') return SEGMENT_MAP[c - 'a' + 10];
    if (c == '-') return 0x40;
    if (c == '_') return 0x08;
    if (c == ' ') return 0x00;
    return 0x00;
}

esp_err_t tm1637_show_number(tm1637_handle_t handle, int num, bool leading_zero)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    if (num < 0) num = 0;
    if (num > 9999) num = 9999;

    handle->segments[3] = SEGMENT_MAP[num % 10]; num /= 10;
    handle->segments[2] = SEGMENT_MAP[num % 10]; num /= 10;
    handle->segments[1] = SEGMENT_MAP[num % 10]; num /= 10;
    handle->segments[0] = SEGMENT_MAP[num % 10];

    if (!leading_zero) {
        /* 去除前导零 */
        for (int i = 0; i < 3; i++) {
            if (handle->segments[i] == SEGMENT_MAP[0]) {
                handle->segments[i] = 0x00;
            } else {
                break;
            }
        }
    }

    tm1637_refresh(handle);
    return ESP_OK;
}

esp_err_t tm1637_show_string(tm1637_handle_t handle, const char *str)
{
    if (!handle || !str) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < 4; i++) {
        if (str[i] == '\0') {
            handle->segments[i] = 0x00;
        } else {
            handle->segments[i] = char_to_segment(str[i]);
        }
    }
    tm1637_refresh(handle);
    return ESP_OK;
}

esp_err_t tm1637_show_raw(tm1637_handle_t handle, const uint8_t segments[4])
{
    if (!handle || !segments) return ESP_ERR_INVALID_ARG;
    memcpy(handle->segments, segments, 4);
    tm1637_refresh(handle);
    return ESP_OK;
}

esp_err_t tm1637_clear(tm1637_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    memset(handle->segments, 0, 4);
    handle->colon = false;
    tm1637_refresh(handle);
    return ESP_OK;
}

esp_err_t tm1637_set_colon(tm1637_handle_t handle, bool on)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    handle->colon = on;
    tm1637_refresh(handle);
    return ESP_OK;
}
