# ESP32 开发环境与项目文档

## 目录

- [1. 环境信息](#1-环境信息)
- [2. 基础环境安装](#2-基础环境安装)
- [3. ESP-IDF 安装](#3-esp-idf-安装)
- [4. 项目一：蓝牙 SPP 回声服务器 (bt_echo)](#4-项目一蓝牙-spp-回声服务器-bt_echo)
- [5. 项目二：WiFi AP + TCP 回声服务器 (wifi_echo)](#5-项目二wifi-ap--tcp-回声服务器-wifi_echo)
- [5.5 项目三：micro-ROS 硬件控制节点 (wifi_echo_micro_ros)](#55-项目三micro-ros-硬件控制节点-wifi_echo_micro_ros)
- [6. 双网络配置（有线 Internet + WiFi 控制 ESP32）](#6-双网络配置有线-internet--wifi-控制-esp32)
- [7. 客户端工具](#7-客户端工具)
- [8. 常用命令速查](#8-常用命令速查)
- [9. 通信距离参考](#9-通信距离参考)
- [10. 项目文件结构](#10-项目文件结构)
- [11. 故障排查](#11-故障排查)

---

## 1. 环境信息

### 主机

| 项目 | 值 |
|---|---|
| 操作系统 | Ubuntu 24.04 LTS (Noble Numbat) |
| 内核版本 | 6.17.0-19-generic |
| 有线网卡 | eno1 (10.161.176.50/24, 网关 10.161.176.xxx) |
| 无线网卡 | wlp195s0 (MediaTek MT7925, PCIe, 驱动 mt7925e) |
| WiFi 芯片 | MediaTek MT7925 (PCI ID: 14c3:0717) |
| 蓝牙状态 | 正常工作 |
| ESP-IDF 版本 | v5.4 |
| ESP-IDF 路径 | ~/esp/esp-idf |

### ESP32 开发板

| 项目 | 值 |
|---|---|
| 开发板类型 | ESP32-DevKitC (PCB 板载天线) |
| 芯片型号 | ESP32-D0WDQ6 (revision v1.0) |
| 芯片功能 | Wi-Fi, Bluetooth, 双核 + LP Core, 240MHz |
| 晶振频率 | 40MHz |
| WiFi 发射功率 | 最高 +20 dBm |
| 蓝牙发射功率 | +9 dBm (Class 2) |
| WiFi MAC | fc:f5:c4:16:24:a4 |
| 蓝牙 MAC | fc:f5:c4:16:24:a6 |
| USB 串口芯片 | Silicon Labs CP2102 (VID:PID = 10c4:ea60) |
| 串口设备 | /dev/ttyUSB0 |
| 波特率 | 115200 (串口监控), 460800 (烧录) |

---

## 2. 基础环境安装

### 一键安装

```bash
cd ~/Documents/esp32
chmod +x setup_esp32.sh
./setup_esp32.sh
```

### 手动安装系统依赖

```bash
sudo apt-get update
sudo apt-get install -y \
    git wget flex bison gperf \
    python3 python3-pip python3-venv \
    cmake ninja-build ccache \
    libffi-dev libssl-dev \
    dfu-util libusb-1.0-0
```

### 安装 esptool

```bash
pip3 install --break-system-packages esptool
sudo pip3 install --break-system-packages esptool
```

### 配置串口权限

```bash
sudo usermod -aG dialout $USER
# 需要注销重新登录生效
```

### 检测 ESP32 板子

```bash
# 查看串口设备
ls -l /dev/ttyUSB* /dev/ttyACM*

# 查看 USB 设备
lsusb | grep -iE "cp210|ch340|ch341|ftdi|silicon|espressif"

# 读取芯片信息
python3 -m esptool --port /dev/ttyUSB0 chip-id
```

---

## 3. ESP-IDF 安装

### 克隆 ESP-IDF

```bash
mkdir -p ~/esp
cd ~/esp
git clone --recursive --depth 1 --branch v5.4 https://github.com/espressif/esp-idf.git
```

### 安装工具链

```bash
cd ~/esp/esp-idf
./install.sh esp32
```

### 激活环境（每次新终端都需要执行）

```bash
export IDF_PATH=~/esp/esp-idf
source "$IDF_PATH/export.sh"
```

> 建议将以上两行添加到 `~/.bashrc`，或在需要时手动执行。

---

## 4. 项目一：蓝牙 SPP 回声服务器 (bt_echo)

### 功能

- ESP32 作为经典蓝牙 SPP 服务器，设备名 `ESP32-Echo`
- 接收任意消息，回复 `收到: <原始消息>`
- 支持 SSP (Secure Simple Pairing) 认证

### 项目结构

```
bt_echo/
├── CMakeLists.txt          # 顶层 CMake 配置
├── sdkconfig.defaults      # 蓝牙配置 (经典 BT + SPP)
└── main/
    ├── CMakeLists.txt      # 组件 CMake 配置
    └── main.c              # 主程序 (SPP 回调 + 收发逻辑)
```

### 关键配置 (sdkconfig.defaults)

```
CONFIG_BT_ENABLED=y
CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_SPP_ENABLED=y
CONFIG_BT_BLE_ENABLED=n
CONFIG_BT_SSP_ENABLED=y
```

### 编译与烧录

```bash
export IDF_PATH=~/esp/esp-idf && source "$IDF_PATH/export.sh"
cd ~/Documents/esp32/bt_echo

idf.py set-target esp32
idf.py build
sudo python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_size 2MB --flash_freq 40m \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/bt_echo.bin
```

### 测试方法

1. **Android 手机**: 安装 "Serial Bluetooth Terminal" App
2. 搜索并配对 `ESP32-Echo`（PIN 码: 1234）
3. 连接后发送任意文字，收到 `收到: xxx` 回复

### 工作流程

```
手机/电脑 蓝牙 SPP 连接
        │
        ▼
  ESP32 SPP 服务器
  (ESP_SPP_DATA_IND_EVT)
        │
        ▼
  解析数据 → 构造 "收到: " + 原文
        │
        ▼
  esp_spp_write() 回复
```

---

## 5. 项目二：WiFi AP + TCP JSON 控制服务器 (wifi_echo)

### 功能

- ESP32 创建 WiFi 热点 (SSID: `ESP32-Control`, 密码: `esp32ctrl`)
- 启动 TCP 服务器监听端口 3333
- **JSON 协议**远程控制 5 个模块: 蜂鸣器、舵机、数码管、LCD、系统
- 支持多客户端并发连接 (最多 4 个)

### 硬件外设

| 组件 | GPIO | 说明 |
|------|------|------|
| 无源蜂鸣器 | 25 | LEDC PWM, 串联 100Ω |
| 舵机 (180°) | 27 | LEDC PWM 50Hz, 500~2500μs |
| TM1637 数码管 CLK | 16 | 四位七段数码管时钟线 |
| TM1637 数码管 DIO | 17 | 四位七段数码管数据线 (需 5V VCC) |
| Grove LCD SDA | 18 | I2C, 16x2 字符 LCD + RGB 背光 |
| Grove LCD SCL | 23 | I2C 时钟线 |

### 项目结构

```
wifi_echo/
├── CMakeLists.txt          # 顶层 CMake 配置
├── sdkconfig.defaults      # WiFi AP 配置
├── PROTOCOL.md             # 完整 JSON 通信协议文档
├── build_flash.sh          # 一键编译烧录脚本
├── monitor.sh              # 串口监控脚本
├── run.sh                  # 运行脚本 (连WiFi + 启动客户端)
├── stop.sh                 # 停止脚本 (断开WiFi + 清理路由)
├── wifi_client.py          # 交互式 TCP 客户端 (JSON + 快捷命令)
├── main/
│   ├── CMakeLists.txt
│   └── main.c              # 主程序 (WiFi AP + TCP 服务器 + JSON 分发)
└── components/
    ├── buzzer/              # 蜂鸣器驱动 (LEDC PWM)
    ├── servo/               # 舵机驱动 (LEDC PWM 50Hz)
    ├── tm1637/              # TM1637 四位七段数码管驱动
    ├── grove_lcd/           # Grove LCD RGB 16x2 驱动 (I2C)
    └── cmd_handler/         # JSON 命令解析与模块分发
        ├── cmd_handler.c    # 核心分发器
        ├── cmd_buzzer.c     # 蜂鸣器命令
        ├── cmd_servo.c      # 舵机命令
        ├── cmd_display.c    # 数码管命令
        └── cmd_lcd.c        # LCD 命令
```

### JSON 协议示例

```json
{"cmd":"servo","act":"set","angle":90}
{"cmd":"buzzer","act":"beep","count":3}
{"cmd":"display","act":"number","value":1234}
{"cmd":"lcd","act":"print","row":0,"text":"Hello"}
{"cmd":"lcd","act":"rgb","r":0,"g":0,"b":255}
```

详细协议见 [wifi_echo/PROTOCOL.md](wifi_echo/PROTOCOL.md)，README 见 [wifi_echo/README.md](wifi_echo/README.md)。

### 快速使用

```bash
cd ~/Documents/esp32/wifi_echo

# 编译并烧录 (首次或代码修改后)
./build_flash.sh

# 监控串口输出 (查看 ESP32 日志, Ctrl+] 退出)
./monitor.sh

# 配置双网络 (有线上网 + WiFi 控 ESP32)
bash ~/Documents/esp32/setup_dual_network.sh

# 运行交互式客户端
python3 wifi_client.py

# 快捷命令示例
# >> servo 90
# >> beep 3
# >> lcd Hello
# >> rgb 0 0 255
```

### 脚本说明

| 脚本 | 功能 | 参数 |
|---|---|---|
| `build_flash.sh` | 编译 + 烧录固件 | `[PORT]` 默认 /dev/ttyUSB0 |
| `monitor.sh` | 串口监控 ESP32 日志 | `[PORT]` 默认 /dev/ttyUSB0 |
| `run.sh` | 连接 WiFi + 验证 + 启动客户端 | 无 |
| `stop.sh` | 断开 WiFi + 清理路由 | 无 |
| `wifi_client.py` | 交互式 TCP 客户端 | `[IP] [PORT]` |

### WiFi AP 参数

| 参数 | 值 |
|---|---|
| SSID | ESP32-Control |
| 密码 | esp32ctrl |
| 认证模式 | WPA2-PSK |
| 频道 | 6 |
| 最大客户端 | 4 |
| ESP32 IP | 192.168.4.1 |
| DHCP 范围 | 192.168.4.2 ~ 192.168.4.5 |
| TCP 端口 | 3333 |

### 编译与烧录

```bash
export IDF_PATH=~/esp/esp-idf && source "$IDF_PATH/export.sh"
cd ~/Documents/esp32/wifi_echo

idf.py set-target esp32
idf.py build
~/.local/bin/esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_size 4MB --flash_freq 40m \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/wifi_echo.bin
```

### 启动后串口日志

```
I WIFI_CTRL: === ESP32 WiFi 控制服务器 ===
I BUZZER: 蜂鸣器初始化完成 (GPIO 25)
I SERVO: 舵机初始化完成 (GPIO 27, 500~2500μs, 0~180°)
I WIFI_CTRL: 数码管已就绪
I GROVE_LCD: Grove LCD RGB 初始化完成 (SDA=18, SCL=23, I2C0)
I WIFI_CTRL: Grove LCD 已就绪
I WIFI_CTRL: WiFi AP 已启动
I WIFI_CTRL:   SSID:     ESP32-Control
I WIFI_CTRL:   密码:     esp32ctrl
I WIFI_CTRL:   频道:     6
I WIFI_CTRL:   IP:       192.168.4.1
I WIFI_CTRL:   TCP 端口: 3333
I WIFI_CTRL: TCP 服务器已启动, 监听端口 3333
I WIFI_CTRL: 设备已就绪
```

### 工作流程

```
  Ubuntu (wlp195s0)                    ESP32
       │                                │
       │ 1. WiFi 连接 ESP32-Control     │
       │ ─────────────────────────────► │
       │   获取 IP: 192.168.4.x         │
       │                                │
       │ 2. TCP connect 192.168.4.1:3333│
       │ ─────────────────────────────► │
       │                                │
       │ 3. send("Hello")              │
       │ ─────────────────────────────► │
       │                                │ 处理: "收到: " + "Hello"
       │                                │
       │ 4. recv("收到: Hello")         │
       │ ◄───────────────────────────── │
```

### 测试验证结果

```
发送: Hello ESP32  →  回复: 收到: Hello ESP32  ✅
发送: 你好世界     →  回复: 收到: 你好世界     ✅
发送: test 123     →  回复: 收到: test 123     ✅
```

---

## 5.5 项目三：micro-ROS 硬件控制节点 (wifi_echo_micro_ros)

用 ROS 2 micro-ROS 替代 TCP JSON 协议，ESP32 作为 ROS 2 节点（`/esp32/esp32_controller`），通过 XRCE-DDS 与 Docker micro-ROS Agent 通信。

### 架构

```
Ubuntu (ROS 2 Jazzy)          ESP32 (micro-ROS)
┌───────────────────┐          ┌──────────────────────┐
│ ros2 topic pub/sub│◄──UDP──►│ micro-ROS Agent      │
│                   │  8888    │ (Docker: jazzy)      │
│ /esp32/servo_cmd  │──────►  │ servo (GPIO 18)      │
│ /esp32/buzzer_cmd │──────►  │ buzzer (GPIO 25)     │
│ /esp32/display_cmd│──────►  │ TM1637 (CLK17/DIO16) │
│ /esp32/lcd_cmd    │──────►  │ Grove LCD (I2C)      │
│ /esp32/heartbeat  │◄──────  │ uptime (5s)          │
│ /esp32/servo_state│◄──────  │ angle feedback       │
└───────────────────┘          └──────────────────────┘
       WiFi Hotspot: 192.168.100.1/24 (wlp195s0)
```

### 快速启动

```bash
# 1. 启动 WiFi 热点
sudo nmcli device wifi hotspot ifname wlp195s0 ssid "Ubuntu-ROS" password "ros2ctrl"

# 2. 启动 Docker micro-ROS Agent
docker run -d --rm --name micro-ros-agent --net=host \
    microros/micro-ros-agent:jazzy udp4 --port 8888 -v6

# 3. 编译烧录 (首次)
cd wifi_echo_micro_ros
source ~/esp/esp-idf/export.sh
idf.py set-target esp32 && idf.py build flash -p /dev/ttyUSB0

# 4. 测试
source /opt/ros/jazzy/setup.bash
ros2 topic list               # 应显示 6 个 /esp32/* 话题
ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 90.0}"
ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: 3}"
ros2 topic echo /esp32/heartbeat
```

> **重要**: micro-ROS Agent 必须使用 Docker 版本 (`microros/micro-ros-agent:jazzy`)，snap 版本不兼容。  
> 详细说明见 [wifi_echo_micro_ros/README.md](wifi_echo_micro_ros/README.md)

---

## 6. 双网络配置（有线 Internet + WiFi 控制 ESP32）

### 网络拓扑

```
                    ┌──────────────┐
  Internet ◄──────► │  eno1 (有线)  │ 10.161.176.50/24
                    │              │   网关: 10.161.176.254
                    │   Ubuntu     │
                    │              │
  ESP32    ◄──────► │ wlp195s0(WiFi)│ 192.168.4.x
                    └──────────────┘
                           │
                           ▼
                    ┌──────────────┐
                    │   ESP32      │ 192.168.4.1
                    │ WiFi AP 热点  │ SSID: ESP32-Control
                    │ TCP:3333     │
                    └──────────────┘
```

### 路由策略

| 目标 | 接口 | 说明 |
|---|---|---|
| 默认路由 (0.0.0.0/0) | eno1 → 10.161.176.254 | 所有 Internet 流量走有线 |
| 192.168.4.0/24 | wlp195s0 | ESP32 控制流量走 WiFi |

### 一键配置

```bash
cd ~/Documents/esp32
bash setup_dual_network.sh
```

脚本会自动：
1. 检测有线网卡和无线网卡
2. 连接 ESP32 WiFi 热点
3. 配置路由：默认走有线，ESP32 子网走 WiFi
4. 验证 Internet 和 ESP32 连通性

### 手动配置

```bash
# 1. 连接 ESP32 WiFi
sudo nmcli device wifi connect "ESP32-Control" password "esp32ctrl" ifname wlp195s0

# 2. 删除 WiFi 默认路由 (防止 Internet 流量走 WiFi)
sudo ip route del default dev wlp195s0

# 3. 确保有线是默认路由
sudo ip route replace default via 10.161.176.254 dev eno1

# 4. ESP32 子网走 WiFi
sudo ip route replace 192.168.4.0/24 dev wlp195s0

# 5. 验证
ping -c 1 192.168.4.1    # ESP32 ✅
curl http://www.baidu.com  # Internet ✅
```

### 断开 WiFi

```bash
nmcli device disconnect wlp195s0
```

---

## 7. 客户端工具

### WiFi TCP 客户端 (wifi_client.py)

交互式 TCP 客户端，连接 ESP32 WiFi 控制服务器。

```bash
python3 ~/Documents/esp32/wifi_echo/wifi_client.py
```

功能：
- 连接 192.168.4.1:3333
- 输入消息回车发送
- 后台线程接收回复
- 输入 `quit` 退出

可选参数：
```bash
python3 wifi_client.py [IP] [PORT]
# 例如: python3 wifi_client.py 192.168.4.1 3333
```

### 蓝牙 SPP 客户端 (bt_client.py)

蓝牙 RFCOMM 客户端，需要 USB 蓝牙适配器。

```bash
# 先配对
bluetoothctl
> scan on
> pair FC:F5:C4:16:24:A6
> trust FC:F5:C4:16:24:A6
> quit

# 运行客户端
python3 ~/Documents/esp32/bt_echo/bt_client.py
```

---

## 8. 常用命令速查

### ESP-IDF 环境

```bash
# 激活环境
export IDF_PATH=~/esp/esp-idf && source "$IDF_PATH/export.sh"

# 新建项目
idf.py create-project my_project
cd my_project && idf.py set-target esp32

# 编译
idf.py build

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 串口监控
idf.py -p /dev/ttyUSB0 monitor

# 编译+烧录+监控一条龙
idf.py -p /dev/ttyUSB0 flash monitor

# menuconfig 配置
idf.py menuconfig

# 清除编译
idf.py fullclean
```

### esptool 操作

```bash
# 读取芯片信息
python3 -m esptool --port /dev/ttyUSB0 chip-id

# 擦除 Flash
python3 -m esptool --port /dev/ttyUSB0 erase_flash

# 烧录固件 (手动)
python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 \
    write_flash 0x0 firmware.bin
```

### 串口监控 (不用 idf.py)

```bash
# 用 Python 读取 ESP32 串口输出
sudo python3 -c "
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
ser.dtr = False; ser.rts = True; time.sleep(0.1)
ser.rts = False; time.sleep(0.1); ser.dtr = False
while True:
    line = ser.readline()
    if line:
        print(line.decode('utf-8', errors='replace').rstrip())
"
```

### 网络调试

```bash
# 查看路由表
ip route show

# 查看网卡状态
ip addr show

# 连接 ESP32 WiFi
sudo nmcli device wifi connect "ESP32-Control" password "esp32ctrl" ifname wlp195s0

# 断开 WiFi
nmcli device disconnect wlp195s0

# 测试 TCP 连接
python3 -c "import socket; s=socket.socket(); s.connect(('192.168.4.1',3333)); s.send(b'test'); print(s.recv(1024)); s.close()"
```

---

## 9. 通信距离参考

### ESP32-DevKitC (PCB 板载天线) 实际距离

| 场景 | WiFi AP 模式 | 经典蓝牙 SPP |
|---|---|---|
| 同一房间无遮挡 | **15-20 米** | **5-8 米** |
| 隔一堵普通墙 | **8-12 米** | **3-5 米** |
| 隔两堵墙 | **5-8 米** | 基本断连 |
| 开阔走廊 | **25-30 米** | **8-10 米** |
| 理论最大距离 | ~50 米 | ~10 米 |

> WiFi 方案的控制距离大约是蓝牙的 **3-5 倍**。

### 影响因素

| 因素 | 说明 |
|---|---|
| 天线类型 | PCB 板载天线距离较短；换 IPEX 外置天线可翻倍 |
| 障碍物 | 金属墙/柜子大幅衰减，普通砖墙影响较小 |
| 干扰 | 2.4 GHz 频段拥挤（其它 WiFi、微波炉）降低有效距离 |
| 发射功率 | WiFi +20 dBm >> 蓝牙 +9 dBm (Class 2) |

### 更远距离方案

| 方案 | 距离 | 说明 |
|---|---|---|
| 外置天线 (IPEX) | WiFi ~100m+ | 需要 ESP32-WROOM-32U 模组或有 IPEX 接口的板子 |
| ESP-NOW 协议 | ~200m (开阔地) | ESP32 专有协议，低延迟点对点，无需路由器 |
| LoRa 模块 | 1-10 km | 需额外硬件 (SX1278 等)，速率低 (~几 kbps) |
| 4G/MQTT | 无限 | 需 SIM 卡模块，走互联网，延迟较高 |

---

## 10. 项目文件结构

```
~/Documents/esp32/
├── README_ESP32_SETUP.md       # 本文档
├── README.md                   # 项目总览
├── MCP_SERVER.md               # MCP 服务器文档
├── AGENT_SKILL_GUIDE.md        # Agent/Skill 使用指南
├── esp32_mcp_server.py         # ESP32 MCP 服务器
├── cpu_monitor.py              # CPU 监控脚本
├── happy_show.py               # 开心动画演示
├── sad_show.py                 # 悲伤动画演示
├── play_game.sh                # 游戏启动脚本
├── setup_esp32.sh              # 基础环境一键安装脚本
├── .vscode/
│   └── mcp.json                # VS Code MCP 配置
├── .github/
│   ├── agents/
│   │   └── esp32.agent.md      # ESP32 Agent 定义
│   └── skills/
│       └── esp32-control/      # ESP32 控制 Skill
├── bt_echo/                    # 蓝牙 SPP 回声服务器项目
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── bt_client.py            # 蓝牙 SPP 客户端
│   └── main/
│       ├── CMakeLists.txt
│       └── main.c
├── wifi_echo/                  # WiFi AP + TCP JSON 控制服务器项目
    ├── CMakeLists.txt
    ├── sdkconfig.defaults
    ├── PROTOCOL.md             # JSON 通信协议文档
    ├── README.md               # wifi_echo 项目说明
    ├── build_flash.sh          # 一键编译烧录
    ├── monitor.sh              # 串口监控
    ├── run.sh                  # 运行 (连WiFi + 客户端)
    ├── stop.sh                 # 停止 (断WiFi + 清路由)
    ├── setup_dual_network.sh   # 双网络配置脚本
    ├── wifi_client.py          # 交互式 TCP 客户端
    ├── main/
    │   ├── CMakeLists.txt
    │   └── main.c
    └── components/
        ├── buzzer/             # 蜂鸣器驱动 (LEDC PWM)
        │   ├── CMakeLists.txt
        │   ├── buzzer.c
        │   └── include/
        ├── servo/              # 舵机驱动 (LEDC PWM 50Hz)
        │   ├── CMakeLists.txt
        │   ├── servo.c
        │   └── include/
        ├── tm1637/             # TM1637 四位七段数码管驱动
        │   ├── CMakeLists.txt
        │   ├── tm1637.c
        │   └── include/
        ├── grove_lcd/          # Grove LCD RGB 16x2 驱动 (I2C)
        │   ├── CMakeLists.txt
        │   ├── grove_lcd.c
        │   └── include/
        └── cmd_handler/        # JSON 命令解析与模块分发
            ├── CMakeLists.txt
            ├── cmd_handler.c   # 核心分发器
            ├── cmd_buzzer.c    # 蜂鸣器命令
            ├── cmd_servo.c     # 舵机命令
            ├── cmd_display.c   # 数码管命令
            ├── cmd_lcd.c       # LCD 命令
            └── include/
└── wifi_echo_micro_ros/        # micro-ROS 硬件控制节点项目
    ├── CMakeLists.txt
    ├── sdkconfig.defaults
    ├── app-colcon.meta         # XRCE-DDS 实体限制配置
    ├── README.md               # 详细使用文档
    ├── main/
    │   ├── CMakeLists.txt
    │   ├── Kconfig.projbuild
    │   └── main.c              # micro-ROS 节点 (esp32_controller)
    └── components/
        ├── buzzer/             # 蜂鸣器驱动
        ├── servo/              # 舵机驱动
        ├── tm1637/             # TM1637 数码管驱动
        ├── grove_lcd/          # Grove LCD RGB 驱动
        └── micro_ros_espidf_component/  # micro-ROS 库 (git clone)

~/esp/esp-idf/                  # ESP-IDF SDK (v5.4)
~/.espressif/                   # ESP-IDF 工具链
```

---

## 11. 故障排查

| 问题 | 解决方案 |
|---|---|
| `Permission denied: '/dev/ttyUSB0'` | `sudo usermod -aG dialout $USER` 后注销重新登录 |
| `idf.py: command not found` | 执行 `source ~/esp/esp-idf/export.sh`（不要用管道） |
| esptool 连接超时 | 按住板上 BOOT 键再执行命令 |
| WiFi 连接后 Internet 断了 | 运行 `setup_dual_network.sh` 修复路由 |
| ESP32 热点搜不到 | 检查固件是否正确烧录，串口查看启动日志 |
| TCP 连接被拒 | 确认已连接 ESP32 WiFi，`ping 192.168.4.1` 测试 |
| 编译报 MACSTR 错误 | ESP-IDF v5.4 中 MACSTR 宏用法变更，避免在 ESP_LOGI 中直接使用 |
| `Not authorized to control networking` | nmcli 命令前加 `sudo` |
| 未检测到 /dev/ttyUSB0 | 检查 USB 线是否支持数据传输（非仅充电线） |
| `sudo python3` 找不到模块 | `sudo pip3 install --break-system-packages <包>`|
