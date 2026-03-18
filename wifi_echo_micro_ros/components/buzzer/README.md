# Buzzer 蜂鸣器驱动库使用文档

## 概述

本库为 ESP32 无源蜂鸣器提供完整驱动，基于 **LEDC PWM** 外设实现。通过调节 PWM 频率产生不同音调，调节占空比控制音量。适用于提示音、报警音、简单旋律播放等场景。

**硬件要求：**
- 无源蜂鸣器（有源蜂鸣器不适用，有源蜂鸣器仅需高低电平驱动）
- 默认连接 GPIO 25（可配置）
- 建议串联 100Ω 限流电阻

**接线示意：**
```
ESP32 GPIO25 ──[100Ω]──┐
                        │
                   [无源蜂鸣器]
                        │
GND ────────────────────┘
```

---

## 文件结构

```
wifi_echo/components/buzzer/
├── CMakeLists.txt           # 组件构建配置
├── buzzer.c                 # 驱动实现
└── include/
    └── buzzer.h             # 公开头文件 (API + 类型定义)
```

---

## 快速开始

### 1. 在代码中引入

```c
#include "buzzer.h"
```

### 2. 初始化蜂鸣器

```c
// 使用默认配置 (GPIO 25, LEDC Timer 0, LEDC Channel 0)
buzzer_init(NULL);

// 或自定义配置
buzzer_config_t cfg = {
    .gpio_num     = 26,   // 改用 GPIO 26
    .ledc_timer   = 1,    // 使用 LEDC Timer 1
    .ledc_channel = 2,    // 使用 LEDC Channel 2
};
buzzer_init(&cfg);
```

### 3. 发出声音

```c
// 发出 1000Hz 的声音，持续 500ms
buzzer_tone_ms(1000, 500);

// 哔两声
buzzer_beep(2);

// 播放开机提示音
buzzer_play_startup();
```

### 4. 不再使用时反初始化

```c
buzzer_deinit();
```

---

## API 详细说明

### `buzzer_init`

```c
esp_err_t buzzer_init(const buzzer_config_t *config);
```

初始化蜂鸣器硬件。配置 LEDC 定时器和通道，初始化后蜂鸣器处于静音状态。

| 参数 | 说明 |
|------|------|
| `config` | 配置结构体指针，传 `NULL` 使用默认配置 |

| 返回值 | 说明 |
|--------|------|
| `ESP_OK` | 成功 |
| `ESP_ERR_*` | LEDC 配置失败 |

> **注意：** 重复调用不会重新初始化，会打印警告并返回 `ESP_OK`。

---

### `buzzer_deinit`

```c
esp_err_t buzzer_deinit(void);
```

反初始化蜂鸣器，停止 PWM 输出并释放资源。反初始化后需重新调用 `buzzer_init` 才能使用。

---

### `buzzer_tone`

```c
esp_err_t buzzer_tone(uint32_t freq_hz);
```

以指定频率**持续发声**，不会自动停止。需要手动调用 `buzzer_stop()` 停止。

| 参数 | 说明 |
|------|------|
| `freq_hz` | 频率，范围 20 ~ 20000 Hz |

| 返回值 | 说明 |
|--------|------|
| `ESP_OK` | 成功 |
| `ESP_ERR_INVALID_STATE` | 未初始化 |
| `ESP_ERR_INVALID_ARG` | 频率超出范围 |

**示例：**
```c
buzzer_tone(440);           // 开始发出 A4 音
vTaskDelay(pdMS_TO_TICKS(1000));
buzzer_stop();              // 1 秒后手动停止
```

---

### `buzzer_tone_ms`

```c
esp_err_t buzzer_tone_ms(uint32_t freq_hz, uint32_t duration_ms);
```

发出指定频率的声音，持续指定毫秒后**自动停止**。此函数为**阻塞调用**，会占用当前任务直到播放完毕。

| 参数 | 说明 |
|------|------|
| `freq_hz` | 频率 (Hz)，传 `0` 或 `NOTE_REST` 表示静音等待 |
| `duration_ms` | 持续时间 (毫秒) |

**示例：**
```c
buzzer_tone_ms(NOTE_C5, 200);   // C5 音 200ms
buzzer_tone_ms(NOTE_REST, 100); // 静音 100ms
buzzer_tone_ms(NOTE_E5, 200);   // E5 音 200ms
```

---

### `buzzer_stop`

```c
esp_err_t buzzer_stop(void);
```

立即停止发声（将 PWM 占空比设为 0）。

---

### `buzzer_set_volume`

```c
esp_err_t buzzer_set_volume(uint8_t volume);
```

设置音量大小。通过调节 PWM 占空比实现，实际有效范围 0~50%（内部限制最大 50% 占空比以保护蜂鸣器）。

| 参数 | 说明 |
|------|------|
| `volume` | 音量百分比，范围 0 ~ 100，默认 50 |

**示例：**
```c
buzzer_set_volume(20);   // 低音量
buzzer_beep(1);

buzzer_set_volume(80);   // 高音量 (实际限制在 50%)
buzzer_beep(1);
```

> **说明：** 无源蜂鸣器的音量与占空比非线性相关，50% 占空比时声音最大。超过 50% 后无实际增益，因此内部限制最大 50%。

---

### `buzzer_beep`

```c
esp_err_t buzzer_beep(int count);
```

发出短促的哔声。每次哔声使用 880Hz (A5)，持续 100ms，间隔 80ms。**阻塞调用**。

| 参数 | 说明 |
|------|------|
| `count` | 哔声次数 |

**示例：**
```c
buzzer_beep(1);    // 单声提示
buzzer_beep(3);    // 三声警告
```

---

### `buzzer_play_melody`

```c
esp_err_t buzzer_play_melody(const buzzer_tone_t *melody, size_t length, uint32_t pause_ms);
```

播放一段自定义旋律。**阻塞调用**，播放完毕后返回。

| 参数 | 说明 |
|------|------|
| `melody` | `buzzer_tone_t` 数组，每个元素包含频率和时长 |
| `length` | 数组长度（音符数量） |
| `pause_ms` | 相邻音符之间的静音间隔 (ms)，建议 30~50 |

**示例 — 播放《小星星》片段：**
```c
static const buzzer_tone_t twinkle[] = {
    { NOTE_C5, 300 },
    { NOTE_C5, 300 },
    { NOTE_G5, 300 },
    { NOTE_G5, 300 },
    { NOTE_A5, 300 },
    { NOTE_A5, 300 },
    { NOTE_G5, 600 },
    { NOTE_REST, 200 },  // 静音间隔
    { NOTE_F5, 300 },
    { NOTE_F5, 300 },
    { NOTE_E5, 300 },
    { NOTE_E5, 300 },
    { NOTE_D5, 300 },
    { NOTE_D5, 300 },
    { NOTE_C5, 600 },
};

buzzer_play_melody(twinkle, sizeof(twinkle) / sizeof(twinkle[0]), 40);
```

---

### `buzzer_play_startup`

```c
esp_err_t buzzer_play_startup(void);
```

播放内置**开机提示音** (C5→E5→G5→C6 上行琶音)。适合在 `app_main` 中系统启动完成时调用。

---

### `buzzer_play_success`

```c
esp_err_t buzzer_play_success(void);
```

播放内置**成功提示音** (C5→E5→G5 短促三音)。适合操作成功时调用。

---

### `buzzer_play_error`

```c
esp_err_t buzzer_play_error(void);
```

播放内置**错误/警告提示音** (A4 反复三声，节奏急促)。适合出错或警告时调用。

---

## 类型定义

### `buzzer_config_t` — 配置结构体

```c
typedef struct {
    int gpio_num;       // GPIO 引脚号, 默认 25
    int ledc_timer;     // LEDC 定时器号 (0-3), 默认 0
    int ledc_channel;   // LEDC 通道号 (0-7), 默认 0
} buzzer_config_t;
```

可使用宏 `BUZZER_CONFIG_DEFAULT()` 获取默认值：
```c
buzzer_config_t cfg = BUZZER_CONFIG_DEFAULT();
cfg.gpio_num = 26;  // 仅修改需要的字段
```

### `buzzer_tone_t` — 音符结构体

```c
typedef struct {
    uint32_t freq_hz;       // 频率 (Hz), 0 = 静音
    uint32_t duration_ms;   // 持续时间 (ms)
} buzzer_tone_t;
```

### `buzzer_note_t` — 预定义音符频率

| 音符 | 枚举值 | 频率 (Hz) |
|------|--------|-----------|
| C4 | `NOTE_C4` | 262 |
| C#4 | `NOTE_CS4` | 277 |
| D4 | `NOTE_D4` | 294 |
| D#4 | `NOTE_DS4` | 311 |
| E4 | `NOTE_E4` | 330 |
| F4 | `NOTE_F4` | 349 |
| F#4 | `NOTE_FS4` | 370 |
| G4 | `NOTE_G4` | 392 |
| G#4 | `NOTE_GS4` | 415 |
| A4 | `NOTE_A4` | 440 |
| A#4 | `NOTE_AS4` | 466 |
| B4 | `NOTE_B4` | 494 |
| C5 | `NOTE_C5` | 523 |
| D5 | `NOTE_D5` | 587 |
| E5 | `NOTE_E5` | 659 |
| F5 | `NOTE_F5` | 698 |
| G5 | `NOTE_G5` | 784 |
| A5 | `NOTE_A5` | 880 |
| B5 | `NOTE_B5` | 988 |
| C6 | `NOTE_C6` | 1047 |
| 静音 | `NOTE_REST` | 0 |

---

## 完整使用示例

```c
#include "buzzer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

void app_main(void)
{
    // 1. 初始化蜂鸣器 (默认 GPIO 25)
    buzzer_init(NULL);

    // 2. 播放开机音
    buzzer_play_startup();
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. 调低音量
    buzzer_set_volume(30);

    // 4. 单声哔
    buzzer_beep(1);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 5. 播放自定义频率
    buzzer_tone_ms(1000, 300);   // 1kHz 300ms
    vTaskDelay(pdMS_TO_TICKS(200));
    buzzer_tone_ms(2000, 300);   // 2kHz 300ms
    vTaskDelay(pdMS_TO_TICKS(500));

    // 6. 播放自定义旋律
    static const buzzer_tone_t my_melody[] = {
        { NOTE_E5,  150 },
        { NOTE_D5,  150 },
        { NOTE_C5,  300 },
        { NOTE_D5,  150 },
        { NOTE_E5,  150 },
        { NOTE_E5,  150 },
        { NOTE_E5,  300 },
    };
    buzzer_play_melody(my_melody, 7, 30);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 7. 成功/错误提示
    buzzer_play_success();
    vTaskDelay(pdMS_TO_TICKS(1000));
    buzzer_play_error();

    // 8. 持续发声模式 (手动控制)
    buzzer_tone(NOTE_A4);        // 开始发声
    vTaskDelay(pdMS_TO_TICKS(2000));
    buzzer_stop();               // 2 秒后停止

    // 9. 不再使用时释放资源
    buzzer_deinit();
}
```

---

## 注意事项

1. **阻塞特性**：`buzzer_tone_ms`、`buzzer_beep`、`buzzer_play_melody` 及所有 `buzzer_play_*` 函数都是阻塞调用。如需非阻塞播放，请在独立的 FreeRTOS 任务中调用。

2. **LEDC 资源冲突**：本库使用 LEDC 定时器和通道，请确保配置的定时器/通道不与项目中其他 LEDC 使用冲突。默认使用 Timer 0 / Channel 0。

3. **线程安全**：本库未加锁，请避免从多个任务同时调用蜂鸣器 API。如有需要，可在应用层加互斥锁。

4. **音量说明**：`buzzer_set_volume` 设置 PWM 占空比。对无源蜂鸣器而言，50% 占空比产生最大声压，内部已做上限保护。

5. **频率范围**：`buzzer_tone` 接受 20Hz ~ 20000Hz。人耳最敏感区间为 1000Hz ~ 4000Hz，提示音建议使用此区间。

6. **GPIO 选择**：ESP32 的 GPIO 25 同时也是 DAC1 输出引脚。如果项目中需要 DAC 功能，请将蜂鸣器改接到其他 GPIO 并在初始化时配置。
