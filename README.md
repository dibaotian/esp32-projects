# ESP32 机械控制前端系统

## 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    MAX+ 395 (大脑)                       │
│                                                         │
│  ┌───────────┐  ┌───────────┐  ┌────────────────────┐  │
│  │  运动规划   │  │  视觉处理  │  │  决策引擎 / AI 推理 │  │
│  └─────┬─────┘  └─────┬─────┘  └─────────┬──────────┘  │
│        └──────────────┼──────────────────┘              │
│                       │                                 │
│              ┌────────┴────────┐                        │
│              │  通信接口管理器   │                        │
│              └────────┬────────┘                        │
└───────────────────────┼─────────────────────────────────┘
                        │
              WiFi / Bluetooth
                        │
┌───────────────────────┼─────────────────────────────────┐
│                       │          ESP32 (控制前端)         │
│              ┌────────┴────────┐                        │
│              │   通信协议层     │                        │
│              └────────┬────────┘                        │
│                       │                                 │
│     ┌─────────┬───────┼───────┬──────────┐             │
│     │         │       │       │          │             │
│  ┌──┴──┐  ┌──┴──┐ ┌──┴──┐ ┌──┴──┐  ┌───┴───┐        │
│  │电机  │  │显示器│ │传感器│ │舵机  │  │ LED/  │        │
│  │驱动  │  │驱动  │ │采集  │ │控制  │  │ 蜂鸣器 │        │
│  └──┬──┘  └──┬──┘ └──┬──┘ └──┬──┘  └───┬───┘        │
│     │        │       │       │          │             │
└─────┼────────┼───────┼───────┼──────────┼─────────────┘
      │        │       │       │          │
   ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐  ┌───┴───┐
   │DC/  │ │OLED/│ │IMU/ │ │SG90/│  │WS2812/│
   │步进  │ │ LCD │ │温度  │ │MG996│  │ 蜂鸣器 │
   │电机  │ │     │ │编码器│ │     │  │       │
   └─────┘ └─────┘ └─────┘ └─────┘  └───────┘
```

## 设计理念

| 层级 | 角色 | 说明 |
|------|------|------|
| **MAX+ 395** | 大脑 | 高层决策、路径规划、视觉处理、AI 推理 |
| **ESP32** | 前端 | 实时外设控制、传感器采集、PWM 输出、中断响应 |

MAX+ 395 负责"想"，ESP32 负责"做"。两者通过无线通信协同工作，实现高层智能与底层实时控制的分离。

## 通信方式

### WiFi（推荐）

- **模式**: ESP32 作为 SoftAP 或连接同一路由器（STA 模式）
- **协议**: TCP Socket，端口 3333
- **带宽**: 高，适合传输大量传感器数据
- **延迟**: ~5-20ms
- **距离**: 室内 15-20m，加外置天线可达 50m+
- **适用场景**: 高频数据交换、远程监控、固定场景

```
MAX+ 395  ──WiFi──>  ESP32 (AP: 192.168.4.1:3333)
```

### Bluetooth（备选）

- **模式**: ESP32 作为 SPP Server（经典蓝牙）或 BLE GATT Server
- **协议**: RFCOMM (SPP) 或 BLE Characteristics
- **带宽**: SPP ~2Mbps / BLE ~1Mbps
- **延迟**: ~10-30ms
- **距离**: 室内 5-10m
- **适用场景**: 低功耗、简单指令下发、移动场景

```
MAX+ 395  ──BT SPP──>  ESP32 (设备名: ESP32-Control)
```

### 通信方式对比

| 特性 | WiFi TCP | Bluetooth SPP | BLE |
|------|----------|---------------|-----|
| 吞吐量 | 高 (~10Mbps) | 中 (~2Mbps) | 低 (~1Mbps) |
| 延迟 | 低 | 中 | 中 |
| 功耗 | 高 | 中 | 低 |
| 连接复杂度 | 低 | 中 | 高 |
| 多设备支持 | 好（多客户端） | 差（1对1） | 好（多从设备） |
| 推荐场景 | 固定机器人 | 简单遥控 | 可穿戴/低功耗 |

## 通信协议设计

MAX+ 395 和 ESP32 之间使用 JSON 格式的命令协议：

### 命令格式（MAX+ 395 → ESP32）

```json
{
  "cmd": "motor",
  "id": 1,
  "action": "set_speed",
  "value": 1500,
  "unit": "rpm"
}
```

### 响应格式（ESP32 → MAX+ 395）

```json
{
  "status": "ok",
  "cmd": "motor",
  "id": 1,
  "data": {
    "speed": 1500,
    "current": 0.35
  }
}
```

### 命令类型一览

| cmd | 说明 | 参数示例 |
|-----|------|---------|
| `motor` | 电机控制 | id, action(set_speed/stop/brake), value |
| `servo` | 舵机控制 | id, angle(0-180) |
| `display` | 显示器控制 | action(clear/text/draw), content |
| `led` | LED 控制 | id, color, brightness |
| `sensor` | 传感器读取 | type(imu/temp/encoder), interval |
| `buzzer` | 蜂鸣器 | freq, duration_ms |
| `system` | 系统命令 | action(reboot/status/version) |
| `config` | 参数配置 | key, value |

### 传感器上报（ESP32 → MAX+ 395）

```json
{
  "type": "sensor",
  "sensor": "imu",
  "data": {
    "accel": [0.01, 0.02, 9.81],
    "gyro": [0.1, -0.2, 0.05],
    "timestamp": 123456789
  }
}
```

## ESP32 外设接线参考

### GPIO 分配建议

| GPIO | 功能 | 外设 | 说明 |
|------|------|------|------|
| 12 | PWM | 电机 A (IN1) | L298N/DRV8833 |
| 13 | PWM | 电机 A (IN2) | L298N/DRV8833 |
| 14 | PWM | 电机 B (IN1) | L298N/DRV8833 |
| 27 | PWM | 电机 B (IN2) | L298N/DRV8833 |
| 25 | PWM | 舵机 1 | SG90/MG996R |
| 26 | PWM | 舵机 2 | SG90/MG996R |
| 21 | SDA | OLED/LCD | I2C 数据线 |
| 22 | SCL | OLED/LCD | I2C 时钟线 |
| 18 | SCK | SPI 设备 | SPI 时钟 |
| 19 | MISO | SPI 设备 | SPI 数据输入 |
| 23 | MOSI | SPI 设备 | SPI 数据输出 |
| 5 | CS | SPI 设备 | SPI 片选 |
| 34 | ADC | 电池电压 | 仅输入 |
| 35 | ADC | 电流检测 | 仅输入 |
| 32 | 数字输出 | 蜂鸣器 | PWM 驱动 |
| 33 | 数字输出 | LED 指示灯 | 状态指示 |
| 4 | 数字输出 | WS2812 LED | 单线协议 |
| 2 | 数字输出 | 板载 LED | 调试指示 |
| 15 | 数字输入 | 编码器 A | 电机反馈 |
| 36 | 数字输入 | 编码器 B | 电机反馈 |

> **注意**: GPIO 6-11 为 SPI Flash 专用，不可使用。GPIO 34-39 仅支持输入。

### 常用外设模块

| 模块 | 接口 | 用途 | 参考型号 |
|------|------|------|----------|
| 电机驱动 | PWM + GPIO | 直流电机控制 | L298N / DRV8833 / TB6612 |
| 舵机 | PWM (50Hz) | 角度控制 | SG90 / MG996R |
| OLED 显示 | I2C | 状态显示 | SSD1306 (0.96"/1.3") |
| LCD 显示 | SPI/I2C | 图形界面 | ST7735 / ILI9341 |
| IMU | I2C/SPI | 姿态感知 | MPU6050 / BMI270 |
| 温湿度 | 单线/I2C | 环境监测 | DHT22 / SHT30 |
| 超声波 | GPIO | 测距 | HC-SR04 |
| 编码器 | 脉冲计数 | 速度/位置反馈 | 霍尔编码器 / 光电编码器 |
| 电流检测 | ADC | 电机负载 | INA219 / ACS712 |
| RGB LED | 单线 | 状态指示 | WS2812B |

## 软件框架

### ESP32 端（ESP-IDF）

```
wifi_echo/              ← 已有基础项目，可扩展
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── main.c          ← 入口：初始化 WiFi/BT + 外设
    ├── comm.c          ← 通信层：WiFi TCP / BT SPP
    ├── comm.h
    ├── motor.c         ← 电机驱动：PWM 控制
    ├── motor.h
    ├── servo.c         ← 舵机驱动
    ├── servo.h
    ├── display.c       ← 显示器驱动
    ├── display.h
    ├── sensor.c        ← 传感器采集
    ├── sensor.h
    ├── cmd_parser.c    ← JSON 命令解析与分发
    └── cmd_parser.h
```

### MAX+ 395 端（Python）

```python
# 示例：MAX+ 395 发送电机控制命令
import socket
import json

def send_command(sock, cmd):
    """发送 JSON 命令到 ESP32"""
    data = json.dumps(cmd).encode('utf-8')
    sock.sendall(data)
    response = sock.recv(1024)
    return json.loads(response.decode('utf-8'))

# 连接 ESP32
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('192.168.4.1', 3333))

# 控制电机
result = send_command(sock, {
    "cmd": "motor",
    "id": 1,
    "action": "set_speed",
    "value": 1000
})
print(f"电机状态: {result}")

# 控制舵机
result = send_command(sock, {
    "cmd": "servo",
    "id": 1,
    "angle": 90
})

# 读取传感器
result = send_command(sock, {
    "cmd": "sensor",
    "type": "imu"
})
print(f"IMU 数据: {result['data']}")

# 显示文字
send_command(sock, {
    "cmd": "display",
    "action": "text",
    "content": "Hello Robot!"
})

sock.close()
```

## 系统工作流程

```
1. 上电初始化
   ESP32: 初始化 GPIO → 初始化外设驱动 → 启动 WiFi AP → 等待连接
   MAX+:  启动系统 → 连接 ESP32 WiFi → 建立 TCP 连接

2. 正常运行循环
   MAX+ 395:
     ├── 传感器融合 & 决策
     ├── 生成控制指令 (JSON)
     └── 发送到 ESP32

   ESP32:
     ├── 接收并解析命令
     ├── 执行外设操作（电机/舵机/显示）
     ├── 采集传感器数据
     └── 返回状态/数据给 MAX+

3. 典型控制周期: 10-50ms (20-100Hz)
```

## 开发路线图

### 第一阶段：通信验证 ✅

- [x] ESP32 WiFi AP + TCP 服务器
- [x] 基础回显通信测试
- [x] MAX+ 395 TCP 客户端连接

### 第二阶段：命令协议

- [ ] JSON 命令解析器（ESP32 端）
- [ ] 命令路由与分发框架
- [ ] 错误处理与状态反馈

### 第三阶段：外设驱动

- [ ] PWM 电机驱动（L298N/DRV8833）
- [ ] 舵机控制（50Hz PWM）
- [ ] OLED 显示（I2C SSD1306）
- [ ] 传感器采集（IMU/温度/编码器）

### 第四阶段：系统集成

- [ ] MAX+ 395 控制框架
- [ ] 传感器数据融合
- [ ] 实时控制回路
- [ ] 异常保护（看门狗/电流过载/通信超时）

### 第五阶段：高级功能

- [ ] OTA 固件升级
- [ ] 多 ESP32 协同控制
- [ ] Web 监控面板
- [ ] 数据记录与回放

## 现有项目说明

| 项目 | 目录 | 状态 | 说明 |
|------|------|------|------|
| WiFi 回显服务 | `wifi_echo/` | ✅ 已完成 | 基础 WiFi AP + TCP 通信，可作为扩展基础 |
| 蓝牙回显服务 | `bt_echo/` | ✅ 已编译 | 蓝牙 SPP 通信，需 BT 适配器测试 |

## 快速开始

```bash
# 1. 设置 ESP-IDF 环境
source ~/esp/esp-idf/export.sh

# 2. 编译并烧录 WiFi 项目（已验证）
cd wifi_echo
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# 3. MAX+ 395 连接 ESP32 WiFi
#    SSID: ESP32-Control
#    密码: esp32ctrl

# 4. 运行客户端测试
python3 wifi_client.py
```

## 参考资源

- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.4/)
- [ESP32 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_cn.pdf)
- [ESP-IDF 外设驱动示例](https://github.com/espressif/esp-idf/tree/master/examples/peripherals)
- [cJSON 库文档](https://github.com/DaveGamble/cJSON)（ESP-IDF 内置，用于 JSON 解析）

---

## 机械手臂控制架构分析

### 当前架构

```
主机 (Python)  ── WiFi TCP JSON ──  ESP32  ── PWM ──  舵机
```

当前 JSON 命令分发框架可用于简单点位控制，但用于机械手臂控制需注意以下问题。

### 问题分析

#### 1. 实时性

| 环节 | 延迟 |
|------|------|
| WiFi 传输 | 1~50ms (正常), 偶尔 100ms+ (丢包重传) |
| TCP 拥塞/Nagle | 额外 0~200ms |
| JSON 解析 | ~1ms |
| 舵机响应 | 20ms (一个 PWM 周期) |
| **总计** | **~20~300ms, 不可预测** |

- **点位控制** (转到某角度停下) → 可以, 延迟不影响最终位置
- **连续轨迹控制** (画圆、跟踪物体) → 不行, 延迟抖动导致运动不平滑
- **实时力控/碰撞检测** → 完全不行

#### 2. 阻塞问题

当前 `servo_smooth_move` 和 `servo_sweep` 是阻塞调用, 执行期间整个客户端连接卡住:
- 无法同时控制多个关节
- 无法中途取消运动
- 无法接收传感器反馈

#### 3. 多关节协同

机械手臂通常 3~6 个舵机, 逐条发 JSON 会有时间差:
```json
{"cmd":"servo1", "act":"set", "angle":90}   ← 先到
{"cmd":"servo2", "act":"set", "angle":45}   ← 晚到几ms~几十ms
```
关节不同步, 末端轨迹偏移。

#### 4. 缺少反馈闭环

当前是开环控制: 发了命令就当执行了, 无法得知舵机实际是否到位、是否堵转/过载。

### 通用方案对比

| 方案 | 适用场景 | 实时性 | 复杂度 |
|------|---------|--------|--------|
| **WiFi + JSON (当前)** | 远程遥控、简单点位 | 差 | 低 |
| **主机规划 + ESP32 执行 (推荐)** | 机械手臂 | 好 | 中 |
| ROS2 + micro-ROS | 专业机器人 | 好 | 高 |
| EtherCAT / CAN 总线 | 工业机器人 | 极好 | 极高 |

### 推荐改进方向

核心思路: **主机负责规划, ESP32 负责实时执行**

```
主机 (大脑)                         ESP32 (肌肉)
─────────                          ──────────
路径规划                            
插值计算                            
  ↓                                
一条命令发送整条轨迹或多关节同步指令        
  ↓ WiFi                           
                              →    本地实时执行器 (自带定时器/任务)
                                   同时驱动多个关节
                                   本地插值, 不依赖网络
                              ←    周期性上报状态
```

具体改进点:

1. **多关节同步命令** — 一条 JSON 控制所有关节:
   ```json
   {"cmd":"arm", "act":"move", "joints":[90, 45, 120, 0, 60, 30]}
   ```

2. **非阻塞执行 + 独立运动任务** — ESP32 用 FreeRTOS 定时器驱动运动, 命令立即返回

3. **轨迹下发** — 主机一次性发多个路径点, ESP32 本地插值执行:
   ```json
   {"cmd":"arm", "act":"trajectory", "points":[
     {"t":0,    "joints":[0, 0, 0]},
     {"t":500,  "joints":[90, 45, 30]},
     {"t":1000, "joints":[180, 90, 60]}
   ]}
   ```

4. **状态反馈** — ESP32 周期性主动上报或按需查询

5. **急停机制** — 高优先级中断命令, 立即停止所有关节

> **结论**: 当前 JSON 协议框架可以复用, 模块化分发器不需要改。需要增加 `arm` 模块处理多关节协同, ESP32 侧实现非阻塞运动控制任务, 协议层面支持批量/轨迹命令。
