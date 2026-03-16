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
