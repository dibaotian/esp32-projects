# ESP32 micro-ROS 控制节点

## 概述

本项目是 [wifi_echo](../wifi_echo/) 的 **micro-ROS 版本**，将 ESP32 从独立 TCP JSON 服务器改造为标准 ROS 2 节点。ESP32 通过 WiFi (STA 模式) 连接路由器，使用 micro-ROS 与 Ubuntu 主机上的 ROS 2 生态通信。

### 与 wifi_echo 的对比

| 特性 | wifi_echo (原版) | wifi_echo_micro_ros (本项目) |
|------|-----------------|---------------------------|
| WiFi 模式 | AP (ESP32 做热点) | STA (ESP32 连 Ubuntu 热点) |
| 通信协议 | 自定义 TCP JSON | DDS/XRCE (ROS 2 标准) |
| 消息格式 | JSON 字符串 | ROS 2 标准消息类型 |
| 客户端 | wifi_client.py | ros2 topic/service, rqt, rviz |
| 网络 | 独立子网 192.168.4.x | Ubuntu 热点子网 192.168.100.x |
| 推送机制 | broadcast_event() | ROS 2 Topic 发布 |
| 可扩展性 | 手动加 cmd_handler 模块 | ROS 2 节点/Topic 任意组合 |
| 依赖 | 无 (纯 ESP-IDF) | ESP-IDF + micro-ROS + ROS 2 主机 |

### 保留不变的部分

以下组件驱动从 wifi_echo 直接复用，**代码不变**：

- `components/buzzer/` — 蜂鸣器驱动 (LEDC PWM)
- `components/servo/` — 舵机驱动 (LEDC PWM 50Hz)
- `components/tm1637/` — TM1637 四位七段数码管驱动
- `components/grove_lcd/` — Grove LCD RGB 16x2 驱动 (I2C)

### 删除/替换的部分

| 原版组件 | 处理 | 替代方案 |
|----------|------|----------|
| `components/cmd_handler/` | 删除 | micro-ROS subscriber 回调 |
| TCP 服务器 (main.c) | 重写 | micro-ROS 节点 |
| JSON 解析 (cJSON) | 删除 | ROS 2 消息反序列化 |
| wifi_client.py | 删除 | ROS 2 命令行 / rqt |

## 系统架构

### 网络拓扑

```
                    ┌──────────────┐
  Internet ◄─────► │  eno1 (有线)  │ 10.161.176.50/24
                    │              │
                    │   Ubuntu     │
                    │              │
  ESP32    ◄─────► │ wlp195s0(AP) │ 192.168.100.1 (WiFi 热点)
                    └──────────────┘
                           │
                           ▼
                    ┌──────────────┐
                    │   ESP32      │ 192.168.100.x (DHCP)
                    │ WiFi STA     │ 连接 Ubuntu-ROS 热点
                    │ micro-ROS    │
                    └──────────────┘
```

与 wifi_echo 的对比：
- **wifi_echo**: ESP32 做热点 (AP)，Ubuntu 连 ESP32 → `192.168.4.x`
- **micro-ROS**: Ubuntu 做热点 (AP)，ESP32 连 Ubuntu → `192.168.100.x`（角色反转）
- 有线网卡 eno1 继续负责 Internet，WiFi 专用于 ESP32 通信，**无需路由器**

### 通信架构

```
  Ubuntu 主机 (ROS 2)                            ESP32 (micro-ROS)
  ┌──────────────────────┐                      ┌──────────────────────────┐
  │  micro-ROS Agent     │◄─── WiFi UDP ───────►│  micro-ROS Client        │
  │  (DDS ↔ XRCE 桥接)  │   192.168.100.x 子网  │                          │
  ├──────────────────────┤                      │  Subscribers:            │
  │                      │                      │    /esp32/servo_cmd      │
  │  你的 ROS 2 节点     │  ros2 topic pub      │    /esp32/buzzer_cmd     │
  │  rqt / rviz2        │──────────────────────►│    /esp32/display_cmd    │
  │  导航 / SLAM        │                      │    /esp32/lcd_cmd        │
  │                      │  ros2 topic echo     │                          │
  │                      │◄─────────────────────│  Publishers:             │
  │                      │                      │    /esp32/heartbeat      │
  │                      │                      │    /esp32/servo_state    │
  ├──────────────────────┤                      │                          │
  │  ros2 service call   │                      │  Services:               │
  │                      │◄────────────────────►│    /esp32/system_info    │
  └──────────────────────┘                      └──────────────────────────┘
```

## ROS 2 接口定义

### Topics (订阅 — 命令下发)

| Topic | 消息类型 | 说明 |
|-------|----------|------|
| `/esp32/servo_cmd` | `std_msgs/msg/Float32` | 舵机目标角度 (0~180) |
| `/esp32/buzzer_cmd` | `std_msgs/msg/Int32` | 蜂鸣器控制: 1~10=beep次数, 21~20000=tone频率Hz, 0=停止, -1/-2/-3=音效 |
| `/esp32/display_cmd` | `std_msgs/msg/Int32` | 数码管: 0~9999=显示数字, -1=清屏 |
| `/esp32/lcd_cmd` | `std_msgs/msg/String` | LCD 第一行显示文字 |

### Topics (发布 — 状态上报)

| Topic | 消息类型 | 频率 | 说明 |
|-------|----------|------|------|
| `/esp32/heartbeat` | `std_msgs/msg/Int32` | 0.2 Hz (5s) | uptime 秒数 |
| `/esp32/servo_state` | `std_msgs/msg/Float32` | 0.2 Hz (5s) | 当前舵机角度 (随心跳一起发布) |

## FreeRTOS 任务模型

| 任务名 | 栈大小 | 优先级 | 说明 |
|--------|--------|--------|------|
| `main` (app_main) | 8192 B | 1 | 初始化外设 + 开机动画 + WiFi STA + 创建 uros 任务 |
| `uros_task` | 16384 B | 5 | micro-ROS executor 主循环 (含心跳定时器 + 4个订阅者) |

### 启动流程

```
app_main()
    │
    ├─ 1. NVS Flash 初始化
    ├─ 2. 外设初始化 (buzzer/servo/tm1637/grove_lcd)
    ├─ 3. 开机动画 (与 wifi_echo 相同)
    ├─ 4. WiFi STA 初始化 → 连接 Ubuntu 热点 (Ubuntu-ROS)
    ├─ 5. 等待获取 IP 地址 (192.168.100.x)
    ├─ 6. micro-ROS 传输层初始化 (UDP → Agent IP:port)
    ├─ 7. xTaskCreate(micro_ros_task) → uros_task 开始 ↓
    │
    │   ── micro_ros_task() 内部 ──
    ├─ 8. 创建 micro-ROS 节点 "esp32_controller" (namespace: esp32)
    ├─ 9. 创建 2 个 publishers (heartbeat, servo_state)
    ├─ 10. 创建 4 个 subscribers (servo_cmd, buzzer_cmd, display_cmd, lcd_cmd)
    ├─ 11. 创建心跳定时器 (5 秒, 发布 uptime + 舵机角度)
    ├─ 12. 初始化 executor (5 handles = 4 sub + 1 timer)
    ├─ 13. LCD 显示 "ROS2 Connected!"
    └─ 14. rclc_executor_spin_some() 无限循环
```

## 硬件配置

与 wifi_echo 完全相同：

| 组件 | GPIO | 说明 |
|------|------|------|
| 无源蜂鸣器 | 25 | LEDC PWM, 串联 100Ω |
| 舵机 (180°) | 27 | LEDC PWM 50Hz, 500~2500μs |
| TM1637 CLK | 16 | 四位七段数码管时钟线 |
| TM1637 DIO | 17 | 四位七段数码管数据线 (需 5V VCC) |
| Grove LCD SDA | 18 | I2C 数据线 |
| Grove LCD SCL | 23 | I2C 时钟线 |

## WiFi 配置

**无需路由器** — Ubuntu 主机用 MT7925 WiFi 网卡创建热点，ESP32 以 STA 模式连接。有线网卡 eno1 继续提供 Internet。

| 参数 | 值 |
|------|------|
| WiFi 模式 | STA (连接 Ubuntu 热点) |
| 热点 SSID | `Ubuntu-ROS` |
| 热点密码 | `ros2ctrl` |
| Ubuntu 热点 IP | `192.168.100.1` (手动配置) |
| ESP32 IP | `192.168.100.x` (DHCP 自动分配) |
| micro-ROS Agent IP | `192.168.100.1` |
| micro-ROS Agent 端口 | 8888 (默认) |

## 环境要求

### Ubuntu 主机

#### 安装 ROS 2 Jazzy (Ubuntu 24.04)

```bash
# 1. 启用 universe 仓库
sudo apt-get install -y software-properties-common
sudo add-apt-repository -y universe

# 2. 添加 ROS 2 GPG 密钥
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg

# 3. 添加 ROS 2 apt 源
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
  http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null

# 4. 更新包索引
sudo apt-get update

# 5. 安装 ROS 2 Jazzy Desktop (含 rviz2, rqt, demo 节点, ~2GB)
sudo apt-get install -y ros-jazzy-desktop

# 6. 安装开发工具 (colcon, rosdep 等)
sudo apt-get install -y ros-dev-tools

# 7. 验证安装
source /opt/ros/jazzy/setup.bash
printenv ROS_DISTRO    # 应输出: jazzy
ros2 pkg list | wc -l  # 应输出: ~286
```

> 建议将 `source /opt/ros/jazzy/setup.bash` 添加到 `~/.bashrc`，每次开终端自动加载。

#### 安装 micro-ROS Agent

推荐使用 **Docker 版本** Agent（与 micro-ROS ESP-IDF 库版本完全匹配）：

```bash
# 拉取 Jazzy 版 Agent 镜像 (首次约 200MB)
docker pull microros/micro-ros-agent:jazzy

# 启动 Agent (--net=host 确保 Agent 监听主机网络)
docker run --rm --net=host microros/micro-ros-agent:jazzy udp4 --port 8888 -v6

# 或后台运行
docker run -d --rm --name micro-ros-agent --net=host \
  microros/micro-ros-agent:jazzy udp4 --port 8888 -v6

# 查看日志
docker logs -f micro-ros-agent

# 停止
docker stop micro-ros-agent
```

> **重要**: snap 版 `micro-ros-agent` 存在与 Jazzy 版 micro-ROS 库的兼容性问题，
> 会导致 Agent session 建立但 ROS 2 实体创建失败。**必须使用 Docker 版本**。

### Ubuntu 创建 WiFi 热点

Ubuntu 主机用 WiFi 网卡 (wlp195s0) 创建热点，替代路由器：

```bash
# 创建热点
sudo nmcli device wifi hotspot ifname wlp195s0 ssid "Ubuntu-ROS" password "ros2ctrl"

# 修改子网为 192.168.100.x (首次需要, 后续会记住)
sudo nmcli connection modify Hotspot ipv4.addresses 192.168.100.1/24 ipv4.method shared
sudo nmcli connection down Hotspot && sudo nmcli connection up Hotspot

# 验证热点已启动
nmcli device show wlp195s0 | grep IP4
# IP4.ADDRESS: 192.168.100.1/24

# Internet 继续走有线 eno1，不受影响
ping -c 1 -I eno1 www.baidu.com
```

| 网卡 | 角色 | IP |
|------|------|------|
| eno1 (有线) | Internet 访问 | 10.161.176.50 |
| wlp195s0 (WiFi) | AP 热点 → ESP32 | 192.168.100.1 |

### ESP32 编译

```bash
# ESP-IDF v5.4 (已安装)
source ~/esp/esp-idf/export.sh

# micro-ROS 组件 (通过 ESP-IDF component manager 自动拉取)
cd wifi_echo_micro_ros
idf.py set-target esp32
idf.py build
```

## 使用步骤

> **完整操作流程**: 需要打开 **3 个终端窗口** — ①热点/网络, ②Agent, ③ESP32 编译烧录 + ROS 2 命令

### 步骤 1: 创建 WiFi 热点 (终端 1)

```bash
# 创建热点 (需要 sudo 权限)
sudo nmcli device wifi hotspot ifname wlp195s0 ssid "Ubuntu-ROS" password "ros2ctrl"
# 成功输出: Device 'wlp195s0' successfully activated with '...'

# 首次需修改子网 (后续 NetworkManager 会记住)
sudo nmcli connection modify Hotspot ipv4.addresses 192.168.100.1/24 ipv4.method shared
sudo nmcli connection down Hotspot && sudo nmcli connection up Hotspot

# 验证热点已启动
nmcli device show wlp195s0 | grep -E "STATE|IP4.ADDRESS"
# 应看到:
#   GENERAL.STATE:    100 (connected)
#   IP4.ADDRESS[1]:   192.168.100.1/24

# 确认 Internet 不受影响 (有线网卡独立工作)
ping -c 2 -I eno1 www.baidu.com
```

> **提示**: 热点在 NetworkManager 重启后可能会失效，需重新执行上述命令。
> 可通过 `nmcli connection show` 查看是否有 `Hotspot` 连接。

### 步骤 2: 启动 micro-ROS Agent (终端 2)

```bash
# 加载 ROS 2 环境
source /opt/ros/jazzy/setup.bash

# 启动 Agent (Docker 版, --net=host 使用主机网络)
docker run --rm --net=host microros/micro-ros-agent:jazzy udp4 --port 8888 -v6
# Agent 会阻塞等待 ESP32 连接, 终端保持打开
# 或后台运行:
# docker run -d --rm --name micro-ros-agent --net=host microros/micro-ros-agent:jazzy udp4 --port 8888 -v6
```

验证 Agent 正在监听 (在另一个终端检查):

```bash
ss -ulnp | grep 8888
# 应看到: UNCONN ... 0.0.0.0:8888

# 查看 Agent 日志 (后台模式)
docker logs -f micro-ros-agent
```

> **日志级别**: `-v6` 显示完整的 XRCE-DDS 消息交互, 调试时很有用。
> 正常运行可用 `-v4` (仅关键事件) 或不加 `-v` (静默模式)。
>
> **重要**: 必须使用 Docker 版 Agent (`microros/micro-ros-agent:jazzy`)。
> snap 版 `micro-ros-agent` 与 Jazzy 版 micro-ROS 库存在兼容性问题。

### 步骤 3: 编译烧录 ESP32 (终端 3)

```bash
# 加载 ESP-IDF 环境
source ~/esp/esp-idf/export.sh
cd ~/Documents/esp32/wifi_echo_micro_ros

# 首次编译 (含 micro-ROS 库构建, 较慢)
idf.py set-target esp32
idf.py build
# 成功输出: Project build complete. To flash, run: idf.py flash

# 烧录到 ESP32
idf.py -p /dev/ttyUSB0 flash

# 查看串口日志 (观察 WiFi 连接和 micro-ROS 初始化)
idf.py -p /dev/ttyUSB0 monitor
# 按 Ctrl+] 退出 monitor
```

串口日志中应看到的关键信息:

```
I (xxx) UROS_CTRL: === ESP32 micro-ROS 控制节点 ===
I (xxx) UROS_CTRL: WiFi 已连接, 启动 micro-ROS 任务
I (xxx) UROS_CTRL: micro-ROS 节点已创建
I (xxx) UROS_CTRL: 执行器已启动, 等待 ROS 2 命令...
```

同时, 终端 2 的 Agent 应显示新客户端连接日志:

```
[1742284800.000000] info | Root.cpp | create_client | ... | client_key: 0x...
[1742284800.000000] info | SessionManager.hpp | establish_session | ...
```

### 步骤 4: 验证 ROS 2 通信 (终端 3, 退出 monitor 后)

```bash
# 加载 ROS 2 环境
source /opt/ros/jazzy/setup.bash

# ---- 检查节点是否上线 ----
ros2 node list
# 应看到: /esp32/esp32_controller

# ---- 查看所有 Topic ----
ros2 topic list
# /esp32/buzzer_cmd
# /esp32/display_cmd
# /esp32/heartbeat
# /esp32/lcd_cmd
# /esp32/servo_cmd
# /esp32/servo_state

# ---- 监听心跳 (每 5 秒一条, 数据为 uptime 秒数) ----
ros2 topic echo /esp32/heartbeat
# data: 35
# ---
# data: 40
# (Ctrl+C 退出)

# ---- 监听舵机状态 (每 5 秒一条) ----
ros2 topic echo /esp32/servo_state
# data: 90.0
```

### 步骤 5: 控制硬件 (终端 3)

```bash
source /opt/ros/jazzy/setup.bash

# ---- 舵机控制 ----
# 转到 45°
ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 45.0}"
# 转到 135°
ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 135.0}"
# 回到 90° (中位)
ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 90.0}"

# ---- 蜂鸣器控制 ----
# 哔 3 声
ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: 3}"
# 播放 1000Hz 音调 (持续 500ms)
ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: 1000}"
# 播放开机音效
ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: -1}"
# 播放成功音效
ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: -2}"
# 停止蜂鸣器
ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: 0}"

# ---- 数码管控制 ----
# 显示数字 1234
ros2 topic pub --once /esp32/display_cmd std_msgs/msg/Int32 "{data: 1234}"
# 显示数字 42
ros2 topic pub --once /esp32/display_cmd std_msgs/msg/Int32 "{data: 42}"
# 清屏
ros2 topic pub --once /esp32/display_cmd std_msgs/msg/Int32 "{data: -1}"

# ---- LCD 控制 ----
# 显示短文本 (≤16 字符, 第一行)
ros2 topic pub --once /esp32/lcd_cmd std_msgs/msg/String "{data: 'Hello ROS2!'}"
# 显示长文本 (>16 字符自动分两行)
ros2 topic pub --once /esp32/lcd_cmd std_msgs/msg/String "{data: 'Line1 16chars!! Line2 here'}"
```

### 步骤 6: 停止服务

```bash
# 终端 2: Ctrl+C 停止 Agent, 或:
docker stop micro-ros-agent
# 终端 1: 关闭热点 (可选)
sudo nmcli connection down Hotspot
```

## 故障排查

| 问题 | 原因 | 解决方法 |
|------|------|----------|
| `ros2 node list` 看不到节点 | Agent 未启动或 ESP32 未连接 WiFi | 检查 Agent 终端是否有连接日志; 检查 ESP32 串口日志 |
| ESP32 串口显示 "Failed status" | Agent 不可达 | 确认热点已启动 (`nmcli dev show wlp195s0`); 确认 Agent 监听 (`ss -ulnp \| grep 8888`); **必须用 Docker 版 Agent** |
| 热点创建失败 | WiFi 网卡被占用 | `sudo nmcli device disconnect wlp195s0` 后重试 |
| Agent 版本不兼容 | snap 版 Agent 与 Jazzy 库不匹配 | 改用 Docker: `docker run --rm --net=host microros/micro-ros-agent:jazzy udp4 --port 8888` |
| ESP32 反复重启 | 栈溢出或初始化失败 | 用 `idf.py monitor` 查看 panic 信息 |
| Topic 发布无反应 | 消息类型不匹配 | 确认使用正确的消息类型 (Float32/Int32/String) |

## 项目结构

```
wifi_echo_micro_ros/
├── CMakeLists.txt              # 项目 CMake
├── README.md                   # 本文档
├── sdkconfig.defaults          # ESP-IDF 配置 (WiFi STA + micro-ROS)
├── main/
│   ├── CMakeLists.txt          # 组件依赖声明
│   ├── Kconfig.projbuild       # 栈大小/优先级配置
│   └── main.c                  # micro-ROS 节点 (subscriber/publisher)
└── components/                 # 从 wifi_echo 复制, 代码不变
    ├── buzzer/                 # 蜂鸣器驱动
    ├── servo/                  # 舵机驱动
    ├── tm1637/                 # TM1637 数码管驱动
    ├── grove_lcd/              # Grove LCD RGB 驱动
    └── micro_ros_espidf_component/  # micro-ROS 库 (git clone)
```

## 与 wifi_echo 的命令对照

| 操作 | wifi_echo (JSON) | micro-ROS (ROS 2) |
|------|-------------------|-------------------|
| 舵机转到 90° | `{"cmd":"servo","act":"set","angle":90}` | `ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 90.0}"` |
| 蜂鸣器响 3 次 | `{"cmd":"buzzer","act":"beep","count":3}` | `ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: 3}"` |
| 数码管显示 1234 | `{"cmd":"display","act":"number","value":1234}` | `ros2 topic pub --once /esp32/display_cmd std_msgs/msg/Int32 "{data: 1234}"` |
| LCD 显示文字 | `{"cmd":"lcd","act":"print","row":0,"text":"Hi"}` | `ros2 topic pub --once /esp32/lcd_cmd std_msgs/msg/String "{data: 'Hi'}"` |
| 查看心跳 | 自动推送 event JSON | `ros2 topic echo /esp32/heartbeat` (Int32: uptime 秒) |

## 快速命令参考

```bash
# ---- 一键启动 (3 个终端分别执行) ----
# 终端1: 热点 (首次需加 modify 子网步骤)
sudo nmcli device wifi hotspot ifname wlp195s0 ssid "Ubuntu-ROS" password "ros2ctrl"

# 终端2: Agent (Docker)
docker run --rm --net=host microros/micro-ros-agent:jazzy udp4 --port 8888 -v6

# 终端3: 烧录 + 测试
source ~/esp/esp-idf/export.sh && cd ~/Documents/esp32/wifi_echo_micro_ros
idf.py -p /dev/ttyUSB0 flash monitor   # Ctrl+] 退出后继续:
source /opt/ros/jazzy/setup.bash
ros2 node list
ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 90.0}"
```

## 后续扩展

- 添加 `/esp32/system_info` Service (`std_srvs/srv/Trigger`) 返回系统信息
- 添加 IMU 传感器 → 发布 `sensor_msgs/msg/Imu`
- 添加电机编码器 → 发布 `nav_msgs/msg/Odometry`
- 接入 Nav2 导航栈
- 接入 SLAM (cartographer / slam_toolbox)
- 多 ESP32 节点组网
