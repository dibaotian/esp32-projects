# ESP32 WiFi 控制服务器

## 概述

ESP32 作为 WiFi AP + TCP 服务器，通过 **JSON 协议** 远程控制 5 个外设模块：蜂鸣器、舵机、数码管、LCD 显示屏。主机通过 WiFi 连接 ESP32 热点，发送 JSON 命令控制硬件。

## 系统架构

```
  主机 (Ubuntu)                          ESP32
  ┌────────────────┐                  ┌──────────────────────────┐
  │  wifi_client.py│──WiFi TCP───────►│  TCP Server :3333        │
  │  或 Python 脚本 │   JSON + \n      │     ↓                    │
  └────────────────┘                  │  JSON 解析 → 模块分发     │
                                      │     ↓                    │
                                      │  ┌─buzzer  (GPIO 25)    │
                                      │  ├─servo   (GPIO 27)    │
                                      │  ├─display (GPIO 16,17) │
                                      │  ├─lcd     (GPIO 18,23) │
                                      │  └─sys                   │
                                      └──────────────────────────┘
```

## 软件架构

### FreeRTOS 任务模型

ESP-IDF 基于 FreeRTOS 实时操作系统，使用多任务并发架构。ESP32 双核 (PRO_CPU + APP_CPU)，FreeRTOS 调度器自动在两核间分配任务。

#### 运行时任务一览

| 任务名 | 栈大小 | 优先级 | 生命周期 | 说明 |
|--------|--------|--------|----------|------|
| `main` (app_main) | 系统默认 | 1 | 启动后返回 | 初始化所有外设，启动开机动画，创建后续任务 |
| `tcp_server` | 4096 B | 5 | 常驻 | TCP 监听，accept 新连接后创建 client 任务 |
| `client` | 8192 B | 5 | 连接期间 | 每个 TCP 客户端一个独立任务，断开后自动删除 |
| `tm_anim` | 2048 B | 3 | 开机动画 | 数码管开机动画 (~10s)，完成后自删除 |
| `sv_anim` | 2048 B | 3 | 开机动画 | 舵机开机动画 (~10s)，完成后自删除 |
| ESP 系统任务 | — | — | 常驻 | WiFi、lwIP、事件循环等由 ESP-IDF 自动管理 |

#### 启动流程

```
芯片上电 / 复位
    │
    ▼
Bootloader (0x1000)
    │  加载分区表 → 加载 app 固件
    ▼
ESP-IDF 系统初始化
    │  FreeRTOS 调度器启动
    │  创建系统任务 (WiFi, lwIP, 事件循环)
    ▼
app_main() 任务开始
    │
    ├─ 1. NVS Flash 初始化          ← 存储 WiFi 配置等参数
    ├─ 2. buzzer_init()             ← LEDC 定时器 + PWM 通道配置
    ├─ 3. servo_init()              ← LEDC 定时器 + 50Hz PWM 配置
    ├─ 4. tm1637_init()             ← GPIO 配置 (CLK/DIO)
    ├─ 5. grove_lcd_init()          ← I2C 总线初始化
    │
    ├─ 6. 创建开机动画任务 (并行)
    │     ├─ tm_anim   任务  ← 数码管动画: 脉冲→解码→锁定→呼吸
    │     ├─ sv_anim   任务  ← 舵机动画:   扫描→探测→抖动→归位
    │     └─ 主线程 LCD 动画  ← LCD 动画:   彩虹→打字机→渐变
    │
    ├─ 7. 等待所有动画完成
    ├─ 8. wifi_init_softap()        ← 创建 WiFi AP 热点
    ├─ 9. 创建 tcp_server 任务      ← 启动 TCP 监听
    └─ 10. buzzer_beep(2)           ← 就绪提示音，app_main 返回
```

### 网络协议栈

```
┌──────────────────────────────────────────────┐
│                 应用层                        │
│   cmd_dispatch() — JSON 命令解析与模块分发      │
├──────────────────────────────────────────────┤
│                 传输层                        │
│   TCP (lwIP) — 端口 3333, SO_REUSEADDR       │
│   accept() → 每连接创建 FreeRTOS 任务          │
├──────────────────────────────────────────────┤
│                 网络层                        │
│   IPv4 — ESP32 AP IP: 192.168.4.1            │
│   DHCP Server — 为客户端分配 192.168.4.2~5    │
├──────────────────────────────────────────────┤
│               WiFi 链路层                     │
│   ESP32 SoftAP — WPA2-PSK, Channel 6         │
│   最多 4 个 STA 客户端并发连接                  │
├──────────────────────────────────────────────┤
│                 硬件                          │
│   ESP32 内置 WiFi (802.11 b/g/n, 2.4 GHz)    │
└──────────────────────────────────────────────┘
```

### 命令处理流水线

TCP 数据到达后的处理流程，全部在 `client` 任务上下文中同步执行：

```
recv(sock)
    │
    ▼
去除尾部 \r\n
    │
    ▼
首字符 == '{' ?
    │
    ├─ 否 → 回声模式: 回复 "收到: <原文>\n"
    │
    └─ 是 → cmd_dispatch(json_str, g_modules)
                │
                ├─ cJSON_Parse()         ← 解析 JSON
                ├─ 提取 cmd + act 字段
                ├─ 遍历 g_modules[] 查找模块
                │       │
                │       ├─ "buzzer"  → cmd_buzzer_handler()
                │       ├─ "servo"   → cmd_servo_handler()
                │       ├─ "display" → cmd_display_handler()
                │       ├─ "lcd"     → cmd_lcd_handler()
                │       └─ "sys"     → cmd_sys_handler()
                │
                ├─ handler 操作硬件 + 填充 resp JSON
                ├─ cJSON_PrintUnformatted(resp)
                └─ 返回 JSON 字符串 → send(sock)
```

### 模块注册表

采用静态数组 + 终止标记模式，零堆分配，编译期确定：

```c
static const cmd_module_t g_modules[] = {
    { "buzzer",  cmd_buzzer_handler  },   // 蜂鸣器
    { "servo",   cmd_servo_handler   },   // 舵机
    { "display", cmd_display_handler },   // 数码管
    { "lcd",     cmd_lcd_handler     },   // LCD
    { "sys",     cmd_sys_handler     },   // 系统
    { NULL, NULL }                        // 终止标记
};
```

每个 handler 签名统一：
```c
esp_err_t handler(const char *action, const cJSON *req, cJSON *resp);
// 返回 ESP_OK → status: "ok"
// 返回 ESP_ERR_NOT_FOUND → "未知动作"
// 返回其他 → "error" + esp_err_to_name()
```

### 外设驱动层

| 组件 | 驱动方式 | ESP-IDF API | 全局句柄 |
|------|----------|-------------|----------|
| 蜂鸣器 | LEDC PWM | `ledc_timer_config` + `ledc_channel_config` | 无 (全局函数) |
| 舵机 | LEDC PWM 50Hz | `ledc_timer_config` (分辨率 16bit) | `g_servo` |
| TM1637 | GPIO 位操作 | `gpio_set_direction` + `gpio_set_level` | `g_tm1637` |
| Grove LCD | I2C 总线 | `i2c_master_bus_add_device` | `g_lcd` |

驱动句柄在 `app_main()` 中创建，通过全局变量 (`extern`) 传递给 `cmd_xxx.c` 命令处理文件。

### 并发与同步

- **TCP 客户端隔离**：每个客户端运行在独立 FreeRTOS 任务中，互不阻塞
- **命令串行执行**：同一客户端的命令在其任务内顺序处理（`smooth`、`sweep`、`tone` 等阻塞命令会阻塞当前客户端）
- **跨客户端竞争**：多个客户端同时操作同一外设时无互斥锁，后到的命令直接覆盖（硬件无损，但行为不确定）
- **开机动画同步**：`tm_anim` 和 `sv_anim` 通过 `volatile bool` 标志通知主线程完成，主线程轮询等待后再启动 WiFi

## 硬件配置

| 组件 | GPIO | 说明 |
|------|------|------|
| 无源蜂鸣器 | 25 | LEDC PWM, 串联 100Ω 限流电阻 |
| 舵机 (180°) | 27 | LEDC PWM 50Hz, 500~2500μs |
| TM1637 CLK | 16 | 四位七段数码管时钟线 |
| TM1637 DIO | 17 | 四位七段数码管数据线 (需 5V VCC) |
| Grove LCD SDA | 18 | I2C 数据线 (16x2 LCD + RGB 背光) |
| Grove LCD SCL | 23 | I2C 时钟线 |

## WiFi 热点参数

| 参数 | 值 |
|------|------|
| SSID | `ESP32-Control` |
| 密码 | `esp32ctrl` |
| 频道 | 6 |
| IP 地址 | `192.168.4.1` |
| TCP 端口 | `3333` |
| 最大客户端 | 4 |

## JSON 通信协议

### 请求格式

```json
{"cmd": "模块名", "act": "动作名", ...参数}
```

所有消息以 `\n` 结尾。非 JSON 消息（不以 `{` 开头）走回声：`收到: <原始消息>`。

### 响应格式

```json
{"status": "ok|error", "cmd": "模块名", "act": "动作名", "msg": "描述"}
```

### 模块命令速查

#### buzzer (蜂鸣器)

| 动作 | JSON | 说明 |
|------|------|------|
| beep | `{"cmd":"buzzer","act":"beep","count":3}` | 哔声 1~20 次 |
| tone | `{"cmd":"buzzer","act":"tone","freq":1000,"ms":500}` | 指定频率+时长 (阻塞) |
| melody | `{"cmd":"buzzer","act":"melody","name":"startup"}` | 播放旋律: startup/success/error |
| volume | `{"cmd":"buzzer","act":"volume","value":30}` | 音量 0~100 |
| stop | `{"cmd":"buzzer","act":"stop"}` | 停止发声 |

#### servo (舵机)

| 动作 | JSON | 说明 |
|------|------|------|
| set | `{"cmd":"servo","act":"set","angle":90}` | 设置角度 0~180 |
| get | `{"cmd":"servo","act":"get"}` | 获取当前角度 |
| smooth | `{"cmd":"servo","act":"smooth","angle":180,"speed":60}` | 平滑移动 (阻塞) |
| sweep | `{"cmd":"servo","act":"sweep","start":0,"end":180,"speed":45,"count":1}` | 往复扫描 (阻塞) |
| pulse | `{"cmd":"servo","act":"pulse","us":1500}` | 原始脉宽 |
| stop | `{"cmd":"servo","act":"stop"}` | 释放力矩 |

#### display (TM1637 数码管)

| 动作 | JSON | 说明 |
|------|------|------|
| number | `{"cmd":"display","act":"number","value":1234}` | 显示 0~9999 |
| text | `{"cmd":"display","act":"text","value":"AbCd"}` | 显示字符 (0-9,A-F,-,空格) |
| raw | `{"cmd":"display","act":"raw","segs":[63,6,91,79]}` | 直接段码 |
| bright | `{"cmd":"display","act":"bright","value":7}` | 亮度 0~7 |
| colon | `{"cmd":"display","act":"colon","on":true}` | 冒号开关 |
| clear | `{"cmd":"display","act":"clear"}` | 清空显示 |

#### lcd (Grove LCD RGB 16x2)

| 动作 | JSON | 说明 |
|------|------|------|
| print | `{"cmd":"lcd","act":"print","row":0,"text":"Hello"}` | 显示文字 (每行最多 16 字符) |
| clear | `{"cmd":"lcd","act":"clear"}` | 清屏 |
| rgb | `{"cmd":"lcd","act":"rgb","r":0,"g":0,"b":255}` | 设置背光颜色 |
| cursor | `{"cmd":"lcd","act":"cursor","col":0,"row":0}` | 设置光标位置 |
| on | `{"cmd":"lcd","act":"on"}` | 开启显示+背光 |
| off | `{"cmd":"lcd","act":"off"}` | 关闭显示+背光 |

#### sys (系统)

| 动作 | JSON | 说明 |
|------|------|------|
| help | `{"cmd":"sys","act":"help"}` | 列出所有模块 |

### 完整协议文档

详见 [PROTOCOL.md](PROTOCOL.md)

## 蜂鸣器提示音

| 场景 | 行为 |
|------|------|
| 设备启动就绪 | 滴滴 (2 声短鸣) |
| WiFi 客户端连接 | 滴滴滴 (3 声短鸣) |
| WiFi 客户端断开 | 滴 (1 声短鸣) |

## LCD 开机显示

开机后 LCD 自动显示 (蓝色背光):
- 第一行: `Hi Min I'm ready`
- 第二行: `Feed me token!`

## 文件结构

```
wifi_echo/
├── CMakeLists.txt              # 项目 CMake 配置
├── sdkconfig                   # ESP-IDF 编译配置
├── sdkconfig.defaults          # 默认配置
├── PROTOCOL.md                 # 完整 JSON 通信协议文档
├── main/
│   ├── CMakeLists.txt          # main 组件配置
│   └── main.c                  # 主程序 (WiFi AP + TCP 服务器 + JSON 分发)
├── components/
│   ├── buzzer/                 # 蜂鸣器驱动 (LEDC PWM)
│   │   ├── include/buzzer.h
│   │   ├── buzzer.c
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   ├── servo/                  # 舵机驱动 (LEDC PWM 50Hz)
│   │   ├── include/servo.h
│   │   ├── servo.c
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   ├── tm1637/                 # TM1637 四位七段数码管驱动
│   │   ├── include/tm1637.h
│   │   ├── tm1637.c
│   │   ├── CMakeLists.txt
│   │   └── README.md
│   ├── grove_lcd/              # Grove LCD RGB 16x2 驱动 (I2C)
│   │   ├── include/grove_lcd.h
│   │   ├── grove_lcd.c
│   │   └── CMakeLists.txt
│   └── cmd_handler/            # JSON 命令解析与分发
│       ├── include/cmd_handler.h
│       ├── cmd_handler.c       # 核心分发器
│       ├── cmd_buzzer.c        # 蜂鸣器命令
│       ├── cmd_servo.c         # 舵机命令
│       ├── cmd_display.c       # 数码管命令
│       ├── cmd_lcd.c           # LCD 命令
│       └── CMakeLists.txt
├── build/                      # 编译输出目录
├── wifi_client.py              # 交互式 TCP 客户端 (支持快捷命令)
├── build_flash.sh              # 编译并烧录脚本
├── setup_dual_network.sh       # 双网络配置脚本
├── run.sh                      # 运行客户端 (连接 WiFi + 启动客户端)
├── monitor.sh                  # 串口监控 ESP32 日志输出
└── stop.sh                     # 断开 ESP32 WiFi 连接
```

## 使用步骤

### 1. 编译并烧录固件

```bash
# 方式一：使用脚本一键编译烧录
./build_flash.sh

# 方式二：手动操作
source ~/esp/esp-idf/export.sh
idf.py build
~/.local/bin/esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_size 4MB --flash_freq 40m \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/wifi_echo.bin
```

烧录完成后 ESP32 自动重启，蜂鸣器发出 2 声短鸣，LCD 显示开机信息。

### 2. 配置主机双网络

主机需要同时连接 ESP32 WiFi 和有线网络（保持上网）：

```bash
bash ~/Documents/esp32/setup_dual_network.sh
```

该脚本会：
1. 连接 ESP32 WiFi 热点 `ESP32-Control`
2. 配置路由：Internet 走有线，ESP32 子网 (`192.168.4.0/24`) 走 WiFi

> **重要**：路由规则重启后丢失，需重新运行脚本。WiFi 连接由 NetworkManager 自动恢复。

### 3. 运行客户端

```bash
python3 wifi_client.py
```

支持 JSON 命令和快捷命令：

| 快捷命令 | 说明 |
|----------|------|
| `beep 3` | 蜂鸣器哔 3 次 |
| `tone 1000 500` | 1kHz 响 500ms |
| `servo 90` | 舵机到 90° |
| `smooth 180 60` | 平滑移动到 180° |
| `sweep 0 180 45 3` | 往复扫描 |
| `angle` | 读取舵机角度 |
| `show AbCd` | 数码管显示文字 |
| `num 1234` | 数码管显示数字 |
| `lcd Hello` | LCD 第一行显示 |
| `lcd2 World` | LCD 第二行显示 |
| `rgb 255 0 0` | LCD 背光红色 |
| `lclear` | LCD 清屏 |
| `help` | 查看模块列表 |

也可直接输入 JSON：`{"cmd":"servo","act":"set","angle":90}`

### 4. 串口监控（调试用）

```bash
./monitor.sh
# 按 Ctrl+] 退出
```

### 5. Python 脚本控制

```python
import socket, json, time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("192.168.4.1", 3333))
sock.settimeout(3)

def cmd(d):
    sock.send((json.dumps(d) + "\n").encode())
    time.sleep(0.15)
    try: return sock.recv(4096).decode()
    except: return ""

cmd({"cmd": "servo", "act": "set", "angle": 90})
cmd({"cmd": "buzzer", "act": "beep", "count": 2})
cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "Hello!"})
cmd({"cmd": "display", "act": "number", "value": 1234})

sock.close()
```

## 常用命令速查

```bash
# 编译
source ~/esp/esp-idf/export.sh && idf.py build

# 烧录
./build_flash.sh

# 配置网络 (重启后需要重新运行)
bash ~/Documents/esp32/setup_dual_network.sh

# 运行客户端
python3 wifi_client.py

# 串口监控
./monitor.sh
```
