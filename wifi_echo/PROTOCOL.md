# ESP32 WiFi 控制通信协议

## 概述

主机与 ESP32 之间通过 **WiFi TCP** 连接，使用 **JSON** 格式进行通信。ESP32 作为 WiFi AP 和 TCP 服务器，主机作为客户端连接。

| 参数 | 值 |
|------|------|
| 传输层 | TCP |
| 端口 | 3333 |
| ESP32 IP | 192.168.4.1 |
| 编码 | UTF-8 |
| 消息格式 | JSON (单行) |
| 消息分隔 | `\n` |

---

## 协议格式

### 请求

```json
{"cmd": "模块名", "act": "动作名", ...参数}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `cmd` | string | 是 | 目标模块: `buzzer`, `servo`, `display`, `lcd`, `sys` |
| `act` | string | 是 | 动作名称 |
| 其他 | any | 否 | 动作参数, 因命令而异 |

### 响应

```json
{"status": "ok|error", "cmd": "模块名", "act": "动作名", "msg": "描述", ...结果}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | `"ok"` 成功, `"error"` 失败 |
| `cmd` | string | 回显请求的模块名 |
| `act` | string | 回显请求的动作名 |
| `msg` | string | 可读描述或错误信息 |
| 其他 | any | 动作特有的返回数据 |

### 非 JSON 消息

如果发送的消息不以 `{` 开头, ESP32 将视为纯文本, 回声返回:

```
发送: hello
返回: 收到: hello
```

---

## 错误响应

### JSON 解析失败

```json
{"status": "error", "msg": "JSON 解析失败"}
```

### 缺少必填字段

```json
{"status": "error", "cmd": "buzzer", "msg": "缺少 act 字段"}
```

### 未知模块

```json
{"status": "error", "cmd": "xxx", "msg": "未知模块: xxx"}
```

### 未知动作

```json
{"status": "error", "cmd": "buzzer", "act": "xyz", "msg": "未知动作: xyz"}
```

### 参数错误

```json
{"status": "error", "cmd": "buzzer", "act": "beep", "msg": "count 范围 1~20"}
```

---

## 模块: buzzer (蜂鸣器)

### beep — 短促哔声

```json
// 请求
{"cmd": "buzzer", "act": "beep", "count": 3}

// 响应
{"status": "ok", "cmd": "buzzer", "act": "beep", "msg": "beep 3"}
```

| 参数 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `count` | int | 否 | 1 | 哔声次数, 范围 1~20 |

### tone — 指定频率发声

```json
// 请求
{"cmd": "buzzer", "act": "tone", "freq": 1000, "ms": 500}

// 响应
{"status": "ok", "cmd": "buzzer", "act": "tone", "msg": "tone 1000Hz 500ms"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `freq` | int | 是 | 频率 (Hz), 范围 20~20000 |
| `ms` | int | 是 | 持续时间 (ms), 最大 10000 |

> **注意**: 此命令为阻塞调用, 在发声结束前不会返回响应。

### melody — 播放内置旋律

```json
// 请求
{"cmd": "buzzer", "act": "melody", "name": "startup"}

// 响应
{"status": "ok", "cmd": "buzzer", "act": "melody", "msg": "melody startup"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 旋律名称 |

可选旋律:

| 名称 | 说明 |
|------|------|
| `startup` | 开机提示音 (C5→E5→G5→C6 上行琶音) |
| `success` | 成功提示音 (C5→E5→G5 短促三音) |
| `error` | 错误警告音 (A4 反复三声) |

### volume — 设置音量

```json
// 请求
{"cmd": "buzzer", "act": "volume", "value": 30}

// 响应
{"status": "ok", "cmd": "buzzer", "act": "volume", "msg": "volume 30"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `value` | int | 是 | 音量百分比, 范围 0~100 |

### stop — 停止发声

```json
// 请求
{"cmd": "buzzer", "act": "stop"}

// 响应
{"status": "ok", "cmd": "buzzer", "act": "stop", "msg": "buzzer stopped"}
```

---

## 模块: servo (舵机)

### set — 设置角度

```json
// 请求
{"cmd": "servo", "act": "set", "angle": 90}

// 响应
{"status": "ok", "cmd": "servo", "act": "set", "msg": "angle=90.0"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `angle` | float | 是 | 目标角度, 范围 0~180 |

### get — 获取当前角度

```json
// 请求
{"cmd": "servo", "act": "get"}

// 响应
{"status": "ok", "cmd": "servo", "act": "get", "angle": 90.0, "msg": "angle=90.0"}
```

**额外返回字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `angle` | float | 当前角度 |

### smooth — 平滑移动

```json
// 请求
{"cmd": "servo", "act": "smooth", "angle": 180, "speed": 60}

// 响应
{"status": "ok", "cmd": "servo", "act": "smooth", "msg": "smooth 180.0° @ 60.0°/s"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `angle` | float | 是 | 目标角度 |
| `speed` | float | 是 | 移动速度 (°/s), 必须 > 0 |

> **注意**: 阻塞调用, 运动完成后才返回响应。

### sweep — 往复扫描

```json
// 请求
{"cmd": "servo", "act": "sweep", "start": 0, "end": 180, "speed": 45, "count": 3}

// 响应
{"status": "ok", "cmd": "servo", "act": "sweep", "msg": "sweep 0.0~180.0° @ 45.0°/s x3"}
```

| 参数 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `start` | float | 是 | — | 起始角度 |
| `end` | float | 是 | — | 终止角度 |
| `speed` | float | 是 | — | 移动速度 (°/s), 必须 > 0 |
| `count` | int | 否 | 1 | 往复次数, 0 = 单程 |

> **注意**: 阻塞调用。

### pulse — 设置原始脉宽

```json
// 请求
{"cmd": "servo", "act": "pulse", "us": 1500}

// 响应
{"status": "ok", "cmd": "servo", "act": "pulse", "msg": "pulse=1500us"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `us` | int | 是 | 脉宽 (μs), 会被限制在 min~max 范围 |

### stop — 释放力矩

```json
// 请求
{"cmd": "servo", "act": "stop"}

// 响应
{"status": "ok", "cmd": "servo", "act": "stop", "msg": "servo stopped"}
```

停止 PWM 输出, 舵机不再保持位置。

---

## 模块: display (数码管)

TM1637 四位七段数码管显示控制。

### number — 显示数字

```json
// 请求
{"cmd": "display", "act": "number", "value": 1234}

// 响应
{"status": "ok", "cmd": "display", "act": "number", "msg": "显示: 1234"}
```

| 参数 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `value` | int | 是 | — | 数值, 范围 0~9999 |
| `leading_zero` | bool | 否 | false | 是否显示前导零 |

### text — 显示字符串

```json
// 请求
{"cmd": "display", "act": "text", "value": "AbCd"}

// 响应
{"status": "ok", "cmd": "display", "act": "text", "msg": "显示: AbCd"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `value` | string | 是 | 最多 4 字符, 支持: 0-9, A-F, `-`, 空格 |

### raw — 直接段码

```json
// 请求
{"cmd": "display", "act": "raw", "segs": [63, 6, 91, 79]}

// 响应
{"status": "ok", "cmd": "display", "act": "raw", "msg": "raw: 0x3F 0x06 0x5B 0x4F"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `segs` | array | 是 | 4 字节段码数组, 每字节 0~255 |

### clear — 清空显示

```json
// 请求
{"cmd": "display", "act": "clear"}

// 响应
{"status": "ok", "cmd": "display", "act": "clear", "msg": "已清空"}
```

### bright — 设置亮度

```json
// 请求
{"cmd": "display", "act": "bright", "value": 7}

// 响应
{"status": "ok", "cmd": "display", "act": "bright", "msg": "亮度=7"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `value` | int | 是 | 亮度等级 0~7 |

### colon — 冒号开关

```json
// 请求
{"cmd": "display", "act": "colon", "on": true}

// 响应
{"status": "ok", "cmd": "display", "act": "colon", "msg": "冒号: 开"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `on` | bool | 是 | true 开启, false 关闭 |

---

## 模块: lcd (Grove LCD RGB)

16x2 字符 LCD，带 RGB 背光。I2C 接口 (SDA=GPIO 18, SCL=GPIO 23)。

### print — 显示文字

```json
// 请求
{"cmd": "lcd", "act": "print", "row": 0, "text": "Hello"}

// 响应
{"status": "ok", "cmd": "lcd", "act": "print", "msg": "行0: Hello"}
```

| 参数 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `text` | string | 是 | — | 显示文字, 最多 16 字符 |
| `row` | int | 否 | 0 | 行号: 0 (第一行) 或 1 (第二行) |

### clear — 清屏

```json
// 请求
{"cmd": "lcd", "act": "clear"}

// 响应
{"status": "ok", "cmd": "lcd", "act": "clear", "msg": "LCD 已清屏"}
```

### rgb — 设置背光颜色

```json
// 请求
{"cmd": "lcd", "act": "rgb", "r": 0, "g": 0, "b": 255}

// 响应
{"status": "ok", "cmd": "lcd", "act": "rgb", "msg": "背光 RGB(0,0,255)"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `r` | int | 是 | 红色 0~255 |
| `g` | int | 是 | 绿色 0~255 |
| `b` | int | 是 | 蓝色 0~255 |

### cursor — 设置光标位置

```json
// 请求
{"cmd": "lcd", "act": "cursor", "col": 0, "row": 0}

// 响应
{"status": "ok", "cmd": "lcd", "act": "cursor", "msg": "光标 (0,0)"}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `col` | int | 是 | 列号 0~15 |
| `row` | int | 是 | 行号 0~1 |

### on — 开启显示

```json
// 请求
{"cmd": "lcd", "act": "on"}

// 响应
{"status": "ok", "cmd": "lcd", "act": "on", "msg": "LCD 已开启"}
```

### off — 关闭显示

```json
// 请求
{"cmd": "lcd", "act": "off"}

// 响应
{"status": "ok", "cmd": "lcd", "act": "off", "msg": "LCD 已关闭"}
```

---

## 模块: sys (系统)

### help — 获取帮助

```json
// 请求
{"cmd": "sys", "act": "help"}

// 响应
{"status": "ok", "cmd": "sys", "act": "help", "msg": "模块: buzzer(...), servo(...), sys(help)"}
```

---

## 客户端快捷命令

Python 客户端 (`wifi_client.py`) 支持快捷命令, 自动转换为 JSON:

| 快捷命令 | 等价 JSON |
|----------|-----------|
| `beep 3` | `{"cmd":"buzzer","act":"beep","count":3}` |
| `tone 1000 500` | `{"cmd":"buzzer","act":"tone","freq":1000,"ms":500}` |
| `melody startup` | `{"cmd":"buzzer","act":"melody","name":"startup"}` |
| `volume 30` | `{"cmd":"buzzer","act":"volume","value":30}` |
| `servo 90` | `{"cmd":"servo","act":"set","angle":90}` |
| `smooth 180 60` | `{"cmd":"servo","act":"smooth","angle":180,"speed":60}` |
| `sweep 0 180 45 3` | `{"cmd":"servo","act":"sweep","start":0,"end":180,"speed":45,"count":3}` |
| `angle` | `{"cmd":"servo","act":"get"}` |
| `stop` | `{"cmd":"servo","act":"stop"}` |
| `show AbCd` | `{"cmd":"display","act":"text","value":"AbCd"}` |
| `num 1234` | `{"cmd":"display","act":"number","value":1234}` |
| `bright 7` | `{"cmd":"display","act":"bright","value":7}` |
| `clear` | `{"cmd":"display","act":"clear"}` |
| `colon on` | `{"cmd":"display","act":"colon","on":true}` |
| `lcd Hello` | `{"cmd":"lcd","act":"print","row":0,"text":"Hello"}` |
| `lcd2 World` | `{"cmd":"lcd","act":"print","row":1,"text":"World"}` |
| `rgb 255 0 0` | `{"cmd":"lcd","act":"rgb","r":255,"g":0,"b":0}` |
| `lclear` | `{"cmd":"lcd","act":"clear"}` |
| `help` | `{"cmd":"sys","act":"help"}` |

也可直接输入原始 JSON (以 `{` 开头), 原样发送。

---

## 交互示例

```
>> beep 3
<< ✓ [buzzer.beep] beep 3

>> servo 90
<< ✓ [servo.set] angle=90.0

>> {"cmd":"servo","act":"get"}
<< ✓ [servo.get] angle=90.0  {'angle': 90.0}

>> smooth 0 30
<< ✓ [servo.smooth] smooth 0.0° @ 30.0°/s

>> melody success
<< ✓ [buzzer.melody] melody success

>> hello
<< 收到: hello

>> {"cmd":"xxx","act":"test"}
<< ✗ [xxx.] 未知模块: xxx
```

---

## 扩展新模块

### ESP32 端

1. 创建 `cmd_xxx.c`, 实现处理函数:

```c
esp_err_t cmd_xxx_handler(const char *action, const cJSON *req, cJSON *resp)
{
    if (strcasecmp(action, "do_something") == 0) {
        // 从 req 取参数
        const cJSON *j = cJSON_GetObjectItem(req, "param");
        // 执行操作...
        CMD_RESP_MSG(resp, "done");
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;  // 未知动作
}
```

2. 在 `main.c` 的模块表中注册:

```c
static const cmd_module_t g_modules[] = {
    { "buzzer", cmd_buzzer_handler },
    { "servo",  cmd_servo_handler  },
    { "xxx",    cmd_xxx_handler    },   // ← 新增一行
    { "sys",    cmd_sys_handler    },
    { NULL, NULL }
};
```

3. 在 `cmd_handler/CMakeLists.txt` 中添加源文件。

### 客户端

在 `wifi_client.py` 的 `shortcut_to_json()` 中添加快捷命令映射 (可选)。
