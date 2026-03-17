# TM1637 四位七段数码管驱动

ESP-IDF 组件，驱动 TM1637 LED 数码管显示模块。

## 特性

- 支持 4 位七段数码管 + 冒号显示
- 数字显示 (0~9999，可选前导零)
- 字符串显示 (0-9, A-F, `-`, `_`, 空格)
- 直接段码控制
- 8 级亮度调节 (0~7)
- 冒号开关控制
- 句柄模式，支持多实例

## 硬件接线

```
ESP32 GPIO16 ──────── TM1637 CLK
ESP32 GPIO17 ──────── TM1637 DIO
ESP32 5V (VIN) ────── TM1637 VCC
ESP32 GND ─────────── TM1637 GND
```

> **重要**: TM1637 模块需要 **5V** 供电才能点亮 LED。信号线 (CLK/DIO) 兼容 3.3V 逻辑电平。

## 快速使用

```c
#include "tm1637.h"

// 使用默认配置 (GPIO 16/17, 亮度 4)
tm1637_config_t cfg = TM1637_CONFIG_DEFAULT();
tm1637_handle_t display;
tm1637_init(&cfg, &display);

// 显示数字
tm1637_show_number(display, 1234, false);

// 显示字符串
tm1637_show_string(display, "AbCd");

// 设置亮度 (0~7)
tm1637_set_brightness(display, 7);

// 开启冒号 (12:34 效果)
tm1637_set_colon(display, true);

// 清空
tm1637_clear(display);

// 释放
tm1637_deinit(display);
```

## API 参考

### 初始化/释放

| 函数 | 说明 |
|------|------|
| `tm1637_init(config, &handle)` | 初始化，返回句柄 |
| `tm1637_deinit(handle)` | 释放资源 |

### 显示控制

| 函数 | 说明 |
|------|------|
| `tm1637_show_number(handle, num, leading_zero)` | 显示数字 0~9999 |
| `tm1637_show_string(handle, str)` | 显示字符串 (最多4字符) |
| `tm1637_show_raw(handle, segments[4])` | 直接设置段码 |
| `tm1637_clear(handle)` | 清空显示 |
| `tm1637_set_brightness(handle, 0~7)` | 设置亮度 |
| `tm1637_set_colon(handle, true/false)` | 冒号开关 |

### 配置结构体

```c
typedef struct {
    int clk_gpio;       // 时钟引脚, 默认 16
    int dio_gpio;       // 数据引脚, 默认 17
    uint8_t brightness; // 亮度 0~7, 默认 4
} tm1637_config_t;
```

## 段码对照表

```
 --a--
|     |
f     b
|     |
 --g--
|     |
e     c
|     |
 --d--  .dp
```

| 字符 | 段码 | 二进制 (dpgfedcba) |
|------|------|--------------------|
| `0`  | 0x3F | 0b00111111 |
| `1`  | 0x06 | 0b00000110 |
| `2`  | 0x5B | 0b01011011 |
| `3`  | 0x4F | 0b01001111 |
| `4`  | 0x66 | 0b01100110 |
| `5`  | 0x6D | 0b01101101 |
| `6`  | 0x7D | 0b01111101 |
| `7`  | 0x07 | 0b00000111 |
| `8`  | 0x7F | 0b01111111 |
| `9`  | 0x6F | 0b01101111 |
| `A`  | 0x77 | 0b01110111 |
| `b`  | 0x7C | 0b01111100 |
| `C`  | 0x39 | 0b00111001 |
| `d`  | 0x5E | 0b01011110 |
| `E`  | 0x79 | 0b01111001 |
| `F`  | 0x71 | 0b01110001 |
| `-`  | 0x40 | 0b01000000 |
| `_`  | 0x08 | 0b00001000 |
| ` `  | 0x00 | 0b00000000 |

## 自定义引脚

```c
tm1637_config_t cfg = {
    .clk_gpio   = 22,  // 自定义 CLK 引脚
    .dio_gpio   = 23,  // 自定义 DIO 引脚
    .brightness = 7,   // 最大亮度
};
tm1637_handle_t display;
tm1637_init(&cfg, &display);
```

## 远程控制

通过 WiFi TCP JSON 协议控制数码管：

```json
{"cmd": "display", "act": "number", "value": 1234}
{"cmd": "display", "act": "text", "value": "AbCd"}
{"cmd": "display", "act": "bright", "value": 7}
{"cmd": "display", "act": "colon", "on": true}
{"cmd": "display", "act": "clear"}
{"cmd": "display", "act": "raw", "segs": [63, 6, 91, 79]}
```

Python 客户端快捷命令：

```
show AbCd     # 显示字符串
num 1234      # 显示数字
bright 7      # 设置亮度
clear         # 清空显示
colon on      # 开启冒号
```

## 通信协议

TM1637 使用类 I2C 的两线串行协议：

- **Start**: CLK 高电平时 DIO 下降沿
- **Stop**: CLK 高电平时 DIO 上升沿
- **数据**: LSB 先发，CLK 上升沿锁存
- **ACK**: 每 8bit 后第 9 个时钟，TM1637 拉低 DIO

命令格式：
1. 数据命令 (0x40): 设置写模式 + 地址自增
2. 地址命令 (0xC0~0xC3): 指定起始位 + 写段码
3. 显示控制 (0x88~0x8F): 开/关显示 + 亮度

## 注意事项

1. **供电**: TM1637 必须用 5V 供电，3.3V 无法点亮 LED
2. **GPIO 16/17**: 在某些 ESP32 模组 (如 WROVER) 上可能被 PSRAM 占用，可改用其他 GPIO
3. **上拉电阻**: TM1637 模块通常自带上拉电阻，无需外加
4. **刷新延时**: 驱动内部每次操作有 50μs 位延时，大量频繁刷新可能影响性能
