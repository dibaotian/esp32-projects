# Servo 舵机驱动库使用文档

## 概述

本库为 ESP32 提供舵机驱动，基于 **LEDC PWM** 外设实现 50Hz PWM 信号输出。采用句柄设计，支持多路舵机独立控制。适用于 SG90、MG996R 等常见舵机。

**硬件要求：**
- 标准 PWM 舵机（50Hz，500~2500μs 脉宽）
- 默认 PWM 引脚 GPIO 27（可配置）
- 舵机需独立供电（5V），不要用 ESP32 的 3.3V 直接驱动

**接线示意：**
```
ESP32 GPIO27 ──────── 舵机信号线 (橙/白)
ESP32 GND ─────────── 舵机地线 (棕/黑)
外部 5V 电源 (+) ──── 舵机电源线 (红)
外部 5V 电源 (-) ──┬─ 舵机地线 (棕/黑)
                   └─ ESP32 GND (共地)
```

> **注意：** ESP32 GPIO 与舵机必须共地，否则 PWM 信号无法正确识别。

---

## 文件结构

```
wifi_echo/components/servo/
├── CMakeLists.txt           # 组件构建配置
├── servo.c                  # 驱动实现
├── README.md                # 本文档
└── include/
    └── servo.h              # 公开头文件 (API + 类型定义)
```

---

## 快速开始

### 1. 在代码中引入

```c
#include "servo.h"
```

### 2. 初始化舵机

```c
// 使用默认配置 (GPIO 27, 0~180°, 500~2500μs)
servo_handle_t my_servo;
servo_init(NULL, &my_servo);

// 或自定义配置
servo_config_t cfg = {
    .gpio_num     = 18,     // 改用 GPIO 18
    .ledc_timer   = 2,      // 使用 LEDC Timer 2
    .ledc_channel = 3,      // 使用 LEDC Channel 3
    .min_pulse_us = 1000,   // 最小脉宽 1000μs
    .max_pulse_us = 2000,   // 最大脉宽 2000μs
    .max_angle    = 270,    // 270° 舵机
};
servo_handle_t custom_servo;
servo_init(&cfg, &custom_servo);
```

### 3. 控制舵机

```c
servo_set_angle(my_servo, 90);              // 转到 90°
servo_smooth_move(my_servo, 180, 60);       // 以 60°/s 平滑转到 180°
servo_sweep(my_servo, 0, 180, 45, 3);       // 0~180° 往复扫描 3 次
```

### 4. 不再使用时释放资源

```c
servo_stop(my_servo);      // 停止 PWM, 释放力矩
servo_deinit(my_servo);    // 释放资源
```

---

## API 详细说明

### `servo_init`

```c
esp_err_t servo_init(const servo_config_t *config, servo_handle_t *handle);
```

初始化舵机，配置 LEDC 定时器和通道。初始状态不输出 PWM。

| 参数 | 说明 |
|------|------|
| `config` | 配置结构体指针，传 `NULL` 使用默认配置 |
| `handle` | 输出参数，返回舵机句柄 |

| 返回值 | 说明 |
|--------|------|
| `ESP_OK` | 成功 |
| `ESP_ERR_INVALID_ARG` | 参数错误 (min_pulse >= max_pulse 等) |
| `ESP_ERR_NO_MEM` | 内存分配失败 |

---

### `servo_deinit`

```c
esp_err_t servo_deinit(servo_handle_t handle);
```

反初始化舵机，停止 PWM 输出并释放内存。调用后句柄不可再使用。

---

### `servo_set_angle`

```c
esp_err_t servo_set_angle(servo_handle_t handle, float angle);
```

立即设置舵机到目标角度。角度会被限制在 `[0, max_angle]` 范围内。

| 参数 | 说明 |
|------|------|
| `angle` | 目标角度 (°)，范围 0 ~ max_angle |

**示例：**
```c
servo_set_angle(my_servo, 0);      // 转到 0°
servo_set_angle(my_servo, 90);     // 转到 90°
servo_set_angle(my_servo, 180);    // 转到 180°
```

---

### `servo_get_angle`

```c
float servo_get_angle(servo_handle_t handle);
```

获取舵机当前所在角度。

| 返回值 | 说明 |
|--------|------|
| >= 0 | 当前角度 (°) |
| -1 | 句柄无效或未初始化 |

---

### `servo_set_pulse_width`

```c
esp_err_t servo_set_pulse_width(servo_handle_t handle, uint32_t pulse_us);
```

直接设置 PWM 脉宽（微秒），适合需要精确控制的场景。脉宽会被限制在 `[min_pulse_us, max_pulse_us]` 范围内。

| 参数 | 说明 |
|------|------|
| `pulse_us` | 脉宽 (μs) |

**示例：**
```c
servo_set_pulse_width(my_servo, 1500);  // 中位 (通常对应 90°)
servo_set_pulse_width(my_servo, 500);   // 最小角度
servo_set_pulse_width(my_servo, 2500);  // 最大角度
```

---

### `servo_smooth_move`

```c
esp_err_t servo_smooth_move(servo_handle_t handle, float angle, float speed);
```

以指定速度平滑移动到目标角度。**阻塞调用**，到达目标后返回。步进间隔约 10ms。

| 参数 | 说明 |
|------|------|
| `angle` | 目标角度 (°) |
| `speed` | 移动速度 (°/s)，如 60 表示每秒转 60° |

**示例：**
```c
servo_set_angle(my_servo, 0);                 // 先到 0°
servo_smooth_move(my_servo, 180, 30);         // 30°/s 转到 180° (约 6 秒)
servo_smooth_move(my_servo, 90, 120);         // 120°/s 快速转到 90° (约 0.75 秒)
```

---

### `servo_sweep`

```c
esp_err_t servo_sweep(servo_handle_t handle, float start, float end, float speed, int count);
```

在两个角度之间往复扫描。**阻塞调用**。

| 参数 | 说明 |
|------|------|
| `start` | 起始角度 (°) |
| `end` | 终止角度 (°) |
| `speed` | 移动速度 (°/s) |
| `count` | 往复次数，0 = 仅单程 (start → end) |

**示例：**
```c
// 雷达式扫描: 0~180° 往复 5 次
servo_sweep(my_servo, 0, 180, 60, 5);

// 小范围摆动: 80~100° 往复 10 次
servo_sweep(my_servo, 80, 100, 90, 10);

// 单程: 从 0° 移动到 90°
servo_sweep(my_servo, 0, 90, 45, 0);
```

---

### `servo_stop`

```c
esp_err_t servo_stop(servo_handle_t handle);
```

停止 PWM 输出，舵机不再保持力矩。适用于到达目标位置后希望节省功耗的场景。

> **注意：** 停止 PWM 后舵机可能因外力而偏移。如需保持位置，不要调用此函数。

---

## 类型定义

### `servo_config_t` — 配置结构体

```c
typedef struct {
    int gpio_num;            // PWM 输出引脚, 默认 27
    int ledc_timer;          // LEDC 定时器号 (0-3), 默认 1
    int ledc_channel;        // LEDC 通道号 (0-7), 默认 1
    uint32_t min_pulse_us;   // 最小脉宽 (μs), 对应 0°, 默认 500
    uint32_t max_pulse_us;   // 最大脉宽 (μs), 对应最大角度, 默认 2500
    uint32_t max_angle;      // 最大角度 (°), 默认 180
} servo_config_t;
```

可使用宏 `SERVO_CONFIG_DEFAULT()` 获取默认值：
```c
servo_config_t cfg = SERVO_CONFIG_DEFAULT();
cfg.gpio_num = 18;  // 仅修改需要的字段
```

### `servo_handle_t` — 舵机句柄

不透明指针类型，每个舵机实例拥有独立句柄。

---

## 多路舵机示例

```c
#include "servo.h"

servo_handle_t servo_pan, servo_tilt;

// 云台水平舵机 (GPIO 27, Channel 1)
servo_config_t pan_cfg = SERVO_CONFIG_DEFAULT();
servo_init(&pan_cfg, &servo_pan);

// 云台垂直舵机 (GPIO 18, Channel 2)
servo_config_t tilt_cfg = {
    .gpio_num     = 18,
    .ledc_timer   = 1,      // 可共享定时器 (同为 50Hz)
    .ledc_channel = 2,      // 必须使用不同通道
    .min_pulse_us = 500,
    .max_pulse_us = 2500,
    .max_angle    = 180,
};
servo_init(&tilt_cfg, &servo_tilt);

// 独立控制
servo_set_angle(servo_pan, 90);
servo_set_angle(servo_tilt, 45);

// 释放
servo_deinit(servo_pan);
servo_deinit(servo_tilt);
```

> **注意：** 多路舵机可以共享同一个 LEDC 定时器（频率相同），但**必须使用不同的 LEDC 通道**。ESP32 共有 8 个 LEDC 通道 (0~7)。

---

## 完整使用示例

```c
#include "servo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

void app_main(void)
{
    // 1. 初始化舵机 (默认 GPIO 27)
    servo_handle_t my_servo;
    servo_init(NULL, &my_servo);

    // 2. 直接设置角度
    servo_set_angle(my_servo, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    servo_set_angle(my_servo, 90);
    vTaskDelay(pdMS_TO_TICKS(1000));

    servo_set_angle(my_servo, 180);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. 平滑移动
    servo_smooth_move(my_servo, 0, 45);     // 45°/s 转到 0°

    // 4. 往复扫描
    servo_sweep(my_servo, 0, 180, 60, 3);   // 60°/s, 3 次往复

    // 5. 精确脉宽控制
    servo_set_pulse_width(my_servo, 1500);  // 中位
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 6. 读取当前角度
    float angle = servo_get_angle(my_servo);
    ESP_LOGI("MAIN", "当前角度: %.1f°", angle);

    // 7. 停止 PWM (释放力矩)
    servo_stop(my_servo);

    // 8. 释放资源
    servo_deinit(my_servo);
}
```

---

## PWM 技术细节

| 参数 | 值 |
|------|------|
| PWM 频率 | 50Hz (周期 20ms) |
| LEDC 分辨率 | 14-bit (16384 级) |
| 每级精度 | ≈ 1.22μs |
| 默认脉宽范围 | 500~2500μs |
| 有效精度 | ≈ 1639 级 (500~2500μs 范围) |
| 角度分辨率 | ≈ 0.11° (180° / 1639 级) |
| 默认 LEDC 资源 | Timer 1, Channel 1 |

---

## 常见舵机脉宽参考

| 舵机型号 | 脉宽范围 | 角度范围 | 配置建议 |
|----------|----------|----------|----------|
| SG90 | 500~2400μs | 0~180° | 使用默认配置即可 |
| MG996R | 500~2500μs | 0~180° | 使用默认配置即可 |
| DS3218 | 500~2500μs | 0~270° | `max_angle = 270` |
| 连续旋转舵机 | 1000~2000μs | — | 1500μs 停止，偏离越多转速越快 |

---

## 注意事项

1. **独立供电**：舵机必须使用外部 5V 电源供电，不能直接从 ESP32 取电，否则可能导致 ESP32 重启或损坏。

2. **共地**：ESP32 与舵机电源必须共地 (GND 连接)，否则 PWM 信号无法正常识别。

3. **LEDC 资源冲突**：默认使用 Timer 1 / Channel 1，与蜂鸣器 (Timer 0 / Channel 0) 不冲突。多路舵机可共享定时器但必须使用不同通道。

4. **阻塞特性**：`servo_smooth_move` 和 `servo_sweep` 是阻塞调用。如需非阻塞控制，请在独立的 FreeRTOS 任务中调用。

5. **线程安全**：同一个句柄不应从多个任务同时操作。不同句柄可在不同任务中独立使用。

6. **力矩释放**：调用 `servo_stop()` 后 PWM 停止输出，舵机不再保持位置。如需持续保持角度，不要调用此函数。

7. **GPIO 选择**：GPIO 27 在 ESP32 上是通用 GPIO，可安全使用。如需更换引脚，请确认所用模组的 GPIO 可用性。
