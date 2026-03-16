# ESP32 开发环境与项目文档

## 目录

- [1. 环境信息](#1-环境信息)
- [2. 基础环境安装](#2-基础环境安装)
- [3. ESP-IDF 安装](#3-esp-idf-安装)
- [4. 项目一：蓝牙 SPP 回声服务器 (bt_echo)](#4-项目一蓝牙-spp-回声服务器-bt_echo)
- [5. 项目二：WiFi AP + TCP 回声服务器 (wifi_echo)](#5-项目二wifi-ap--tcp-回声服务器-wifi_echo)
- [6. 双网络配置（有线 Internet + WiFi 控制 ESP32）](#6-双网络配置有线-internet--wifi-控制-esp32)
- [7. 客户端工具](#7-客户端工具)
- [8. 常用命令速查](#8-常用命令速查)
- [9. 蓝牙适配器问题排查记录](#9-蓝牙适配器问题排查记录)
- [10. 通信距离参考](#10-通信距离参考)
- [11. 项目文件结构](#11-项目文件结构)
- [12. 故障排查](#12-故障排查)

---

## 1. 环境信息

### 主机

| 项目 | 值 |
|---|---|
| 操作系统 | Ubuntu 24.04 LTS (Noble Numbat) |
| 内核版本 | 6.17.0-19-generic |
| 有线网卡 | eno1 (10.161.176.50/24, 网关 10.161.176.254) |
| 无线网卡 | wlp195s0 (MediaTek MT7925, PCIe, 驱动 mt7925e) |
| WiFi 芯片 | MediaTek MT7925 (PCI ID: 14c3:0717) |
| 蓝牙状态 | 硬件支持但 USB 通道故障 (详见第 9 节) |
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

## 5. 项目二：WiFi AP + TCP 回声服务器 (wifi_echo)

### 功能

- ESP32 创建 WiFi 热点 (SSID: `ESP32-Control`, 密码: `esp32ctrl`)
- 启动 TCP 服务器监听端口 3333
- 接收消息回复 `收到: <原始消息>`
- 支持多客户端并发连接 (最多 4 个)

### 项目结构

```
wifi_echo/
├── CMakeLists.txt          # 顶层 CMake 配置
├── sdkconfig.defaults      # WiFi AP 配置
├── build_flash.sh          # 一键编译烧录脚本
├── monitor.sh              # 串口监控脚本
├── run.sh                  # 运行脚本 (连WiFi + 启动客户端)
├── stop.sh                 # 停止脚本 (断开WiFi + 清理路由)
└── main/
    ├── CMakeLists.txt      # 组件 CMake 配置
    └── main.c              # 主程序 (WiFi AP + TCP 服务器)
```

### 快速使用

```bash
cd ~/Documents/esp32/wifi_echo

# 编译并烧录 (首次或代码修改后)
./build_flash.sh

# 监控串口输出 (查看 ESP32 日志, Ctrl+] 退出)
./monitor.sh

# 运行 (自动连 WiFi + 启动客户端)
./run.sh

# 停止 (断开 WiFi, 清理路由)
./stop.sh
```

### 脚本说明

| 脚本 | 功能 | 参数 |
|---|---|---|
| `build_flash.sh` | 编译 + 烧录固件 | `[PORT]` 默认 /dev/ttyUSB0 |
| `monitor.sh` | 串口监控 ESP32 日志 | `[PORT]` 默认 /dev/ttyUSB0 |
| `run.sh` | 连接 WiFi + 验证 + 启动客户端 | 无 |
| `stop.sh` | 断开 WiFi + 清理路由 | 无 |

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
sudo python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_size 2MB --flash_freq 40m \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/wifi_echo.bin
```

### 启动后串口日志

```
I WIFI_ECHO: === ESP32 WiFi 控制服务器 ===
I WIFI_ECHO: WiFi AP 已启动
I WIFI_ECHO:   SSID:     ESP32-Control
I WIFI_ECHO:   密码:     esp32ctrl
I WIFI_ECHO:   频道:     6
I WIFI_ECHO:   IP:       192.168.4.1
I WIFI_ECHO:   TCP 端口: 3333
I WIFI_ECHO: TCP 服务器已启动, 监听端口 3333
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

## 9. 蓝牙适配器问题排查记录

### 问题现象

MT7925 网卡是 WiFi/BT combo 芯片，WiFi 正常工作但蓝牙无法使用。`hcitool dev` 始终为空。

### 诊断步骤与调试命令

#### Step 1: 检查是否有蓝牙 HCI 设备

```bash
hcitool dev
# 正常应该输出 hci0 + MAC 地址
# 本机结果: Devices: (空)
```

#### Step 2: 检查蓝牙内核模块

```bash
lsmod | grep -iE "bluetooth|btusb|btmtk|btintel|btrtl"
```

本机结果: 全部已加载 ✅
```
btusb     77824  0
btrtl     32768  1 btusb
btintel   69632  1 btusb
btbcm     24576  1 btusb
btmtk     36864  1 btusb
bluetooth 1032192 5 btrtl,btmtk,btintel,btbcm,btusb
```

如果模块未加载，手动加载：
```bash
sudo modprobe bluetooth
sudo modprobe btusb
```

#### Step 3: 检查蓝牙服务

```bash
systemctl is-active bluetooth
# 如果是 inactive:
sudo systemctl start bluetooth
sudo systemctl enable bluetooth
```

#### Step 4: 检查 USB 蓝牙设备

```bash
# 查看所有 USB 设备
lsusb

# 搜索已知蓝牙芯片
lsusb | grep -iE "bluetooth|wireless|radio|bt"

# 查看 MediaTek USB 设备 (combo 芯片蓝牙走 USB)
lsusb -d 0e8d:    # MediaTek VID
lsusb -d 14c3:    # MediaTek 另一个 VID

# 查看 USB 设备树
lsusb -t
```

本机结果: 无任何蓝牙 USB 设备出现 ❌

#### Step 5: 检查 dmesg 内核日志（关键步骤）

```bash
# 查看所有蓝牙相关日志
sudo dmesg | grep -iE "bluetooth|btusb|btmtk|hci"

# 查看 USB 枚举错误
sudo dmesg | grep -iE "usb 3-3|unable to enumerate"

# 查看 MT7925 驱动日志
sudo dmesg | grep -iE "mt7925|mt792x|14c3"
```

本机关键错误日志:
```
[  4.411740] usb 3-3: new high-speed USB device number 3 using xhci_hcd
[ 10.001750] usb 3-3: device descriptor read/64, error -110       ← USB 读取超时
[ 25.873769] usb 3-3: device descriptor read/64, error -110
[ 47.860719] usb 3-3: new high-speed USB device number 5 using xhci_hcd
[ 58.737716] usb 3-3: device not accepting address 5, error -62   ← 设备不接受地址
[ 70.001629] usb 3-3: device not accepting address 6, error -62
[ 70.001847] usb usb3-port3: unable to enumerate USB device        ← 最终枚举失败
```

**错误码含义:**
| 错误码 | 含义 | 可能原因 |
|---|---|---|
| error -110 | ETIMEDOUT，USB 操作超时 | 信号线接触不良、供电不足、设备损坏 |
| error -62 | ETIME，定时器超时 | USB 设备无响应 |
| error -71 | EPROTO，协议错误 | 信号质量差或驱动不匹配 |
| error -32 | EPIPE，管道错误 | 端点停止或设备异常 |

#### Step 6: 检查网卡硬件信息

```bash
# 查看 MT7925 PCIe 设备详情
lspci -vvnn -s c3:00.0

# 确认驱动是否加载
lspci -k -s c3:00.0
# 输出: Kernel driver in use: mt7925e

# 查看 WiFi 固件版本
sudo dmesg | grep "mt7925.*Version"
# HW/SW Version: 0x8a108a10, Build Time: 20251210092928a
# WM Firmware Version: ____000000, Build Time: 20251210093025
```

#### Step 7: 检查蓝牙固件文件

```bash
# MT7925 蓝牙固件
ls -la /lib/firmware/mediatek/mt7925/
# BT_RAM_CODE_MT7925_1_1_hdr.bin       (新版，已从上游更新)
# BT_RAM_CODE_MT7925_1_1_hdr.bin.zst   (旧版压缩包)
# BT_RAM_CODE_MT7925_1_1_hdr.bin.zst.bak (备份)

# 查看通用蓝牙固件
ls /lib/firmware/mediatek/ | grep -i bt
```

#### Step 8: rfkill 检查（软/硬件开关）

```bash
rfkill list
# 如果蓝牙被 blocked:
sudo rfkill unblock bluetooth
```

本机结果: rfkill 列表中没有蓝牙设备（因为 HCI 未注册）

### 根因分析

```
MT7925 M.2 网卡 (WiFi/BT Combo)
┌─────────────────────────────────────┐
│  WiFi 模块  ──── PCIe 通道 ──── 主板 PCIe 插槽  ✅ 正常
│                                     │
│  BT 模块    ──── USB 通道  ──── M.2 USB 信号线  ❌ 故障
│                                     │
│  问题: M.2 插槽的 USB 信号未正确连接  │
│  或 USB 信号线接触不良               │
└─────────────────────────────────────┘
```

MT7925 是 WiFi/BT combo 芯片：
- **WiFi** 走 PCIe 通道 → 工作正常 (`mt7925e` 驱动加载成功)
- **蓝牙** 走 USB 内部通道 → USB 枚举失败
- 故障发生在 USB 物理层，在蓝牙固件加载之前，因此更新固件无法解决

### 已尝试的修复

| 操作 | 结果 | 说明 |
|---|---|---|
| `sudo modprobe btusb bluetooth` | ❌ 模块加载但无设备 | 内核模块正常，问题在硬件层 |
| 重启系统 | ❌ 仍然枚举失败 | 每次开机均重试 10 次失败 |
| 更新 BT 固件 (从上游 linux-firmware) | ❌ 无效 | USB 通道断了，固件无法传输到芯片 |
| `sudo systemctl start bluetooth` | ✅ 服务启动 | 但无 HCI 设备可操作 |
| `sudo rfkill unblock bluetooth` | ❌ 无蓝牙设备可 unblock | HCI 未注册 |

### 解决方案

| 方案 | 可行性 | 说明 |
|---|---|---|
| **买 USB 蓝牙适配器** | ✅ 推荐 | CSR8510 (~15元) 或 RTL8761B (~30元)，Linux 免驱 |
| 重新插拔 M.2 网卡 | 可能有效 | 关机断电，清洁金手指，重新对齐插入 |
| 检查 BIOS 蓝牙选项 | 可能有效 | 部分主板可独立启用/禁用蓝牙 |
| 检查 M.2 USB 排线 | 可能有效 | 部分 PCIe WiFi 卡需要额外 USB 排线连主板 |
| **改用 WiFi 方案** | ✅ 已实现 | ESP32 做热点，TCP 通信代替蓝牙 |

### 如果买了 USB 蓝牙适配器后的验证步骤

```bash
# 1. 插入适配器
# 2. 检查是否识别
lsusb | grep -i bluetooth
hcitool dev          # 应看到 hci0

# 3. 启动蓝牙服务
sudo systemctl start bluetooth

# 4. 扫描 ESP32 (需先烧录 bt_echo 固件)
bluetoothctl
> power on
> scan on
# 等待看到 ESP32-Echo (FC:F5:C4:16:24:A6)
> scan off
> pair FC:F5:C4:16:24:A6
# 输入 PIN: 1234 (如果提示)
> trust FC:F5:C4:16:24:A6
> quit

# 5. 运行蓝牙客户端
python3 ~/Documents/esp32/bt_echo/bt_client.py
```

### 实用调试命令汇总

```bash
# === 蓝牙状态总览 ===
hcitool dev                              # 列出 HCI 设备
bluetoothctl show                        # 蓝牙适配器详情
systemctl status bluetooth               # 蓝牙服务状态

# === 内核模块 ===
lsmod | grep -iE "bt|bluetooth"          # 已加载的蓝牙模块
sudo modprobe btusb                      # 加载 btusb 驱动
sudo modprobe -r btusb && sudo modprobe btusb  # 重载驱动

# === USB 设备 ===
lsusb                                    # 列出 USB 设备
lsusb -t                                 # USB 设备树
lsusb -v -d xxxx:xxxx                    # 特定设备详情

# === 内核日志 ===
sudo dmesg | grep -iE "bluetooth|btusb|btmtk|hci"  # 蓝牙日志
sudo dmesg | grep "usb.*error"           # USB 错误
sudo dmesg -w                            # 实时监控 (插拔设备时用)

# === 射频开关 ===
rfkill list                              # 列出所有无线设备
sudo rfkill unblock bluetooth            # 解除蓝牙软屏蔽

# === 固件 ===
ls /lib/firmware/mediatek/mt7925/        # MT7925 固件文件
ls /lib/firmware/intel/                   # Intel 蓝牙固件
ls /lib/firmware/rtl_bt/                  # Realtek 蓝牙固件

# === bluetoothctl 交互调试 ===
bluetoothctl
> power on                               # 开启蓝牙
> agent on                               # 启用配对代理
> default-agent                           # 设为默认代理
> scan on                                # 开始扫描
> devices                                # 列出发现的设备
> pair XX:XX:XX:XX:XX:XX                 # 配对
> trust XX:XX:XX:XX:XX:XX                # 信任
> connect XX:XX:XX:XX:XX:XX              # 连接
> info XX:XX:XX:XX:XX:XX                 # 设备详情
> remove XX:XX:XX:XX:XX:XX               # 移除配对
> quit
```

---

## 10. 通信距离参考

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

## 11. 项目文件结构

```
~/Documents/esp32/
├── README_ESP32_SETUP.md       # 本文档
├── setup_esp32.sh              # 基础环境一键安装脚本
├── setup_dual_network.sh       # 双网络配置脚本
├── bt_echo/                    # 蓝牙 SPP 回声服务器项目
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── bt_client.py            # 蓝牙 SPP 客户端
│   └── main/
│       ├── CMakeLists.txt
│       └── main.c
└── wifi_echo/                  # WiFi AP + TCP 回声服务器项目
    ├── CMakeLists.txt
    ├── sdkconfig.defaults
    ├── build_flash.sh          # 一键编译烧录
    ├── monitor.sh              # 串口监控
    ├── run.sh                  # 运行 (连WiFi + 客户端)
    ├── stop.sh                 # 停止 (断WiFi + 清路由)
    ├── wifi_client.py          # WiFi TCP 客户端
    └── main/
        ├── CMakeLists.txt
        └── main.c

~/esp/esp-idf/                  # ESP-IDF SDK (v5.4)
~/.espressif/                   # ESP-IDF 工具链
```

---

## 12. 故障排查

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
