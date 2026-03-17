# ESP32 MCP Server 文档

## 概述

ESP32 MCP (Model Context Protocol) Server 将 ESP32 硬件控制能力封装为标准 MCP 工具，
使 AI Agent（OpenClaw、GitHub Copilot、Claude Desktop 等）能够直接控制物理硬件。

```
┌────────────────────┐     MCP (stdio)     ┌───────────────────┐    TCP/JSON     ┌──────────┐
│  AI Agent          │ ◄────────────────► │  esp32_mcp_server │ ◄────────────► │  ESP32   │
│  (OpenClaw/Copilot)│    结构化工具调用    │  (Python)         │  192.168.4.1   │  硬件    │
└────────────────────┘                     └───────────────────┘    :3333        └──────────┘
```

---

## 快速开始

### 1. 安装依赖

```bash
pip3 install --break-system-packages mcp
```

### 2. 连接 ESP32 WiFi

```bash
# 方法一: 一键配置双网络
bash ~/Documents/esp32/setup_dual_network.sh

# 方法二: 手动连接
sudo nmcli device wifi connect "ESP32-Control" password "esp32ctrl" ifname wlp195s0
```

### 3. 测试运行

```bash
cd ~/Documents/esp32
python3 esp32_mcp_server.py
```

### 4. 配置到 AI Agent

#### VS Code / GitHub Copilot

在 `.vscode/mcp.json` 中添加:

```json
{
  "servers": {
    "esp32": {
      "command": "python3",
      "args": ["/home/xilinx/Documents/esp32/esp32_mcp_server.py"]
    }
  }
}
```

#### Claude Desktop

在 `~/.config/claude/claude_desktop_config.json` 中添加:

```json
{
  "mcpServers": {
    "esp32": {
      "command": "python3",
      "args": ["/home/xilinx/Documents/esp32/esp32_mcp_server.py"]
    }
  }
}
```

#### OpenClaw / 其他 MCP 客户端

按各客户端文档配置 MCP Server，启动命令为:

```bash
python3 /home/xilinx/Documents/esp32/esp32_mcp_server.py
```

---

## 架构设计

### 三层抽象

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 3: Resources (只读参考)                               │
│  hardware_info, protocol_summary, emotion_reference         │
├─────────────────────────────────────────────────────────────┤
│  Layer 2: Semantic Tools (语义组合)                          │
│  express(), gauge(), reset_hardware(), get_status()         │
├─────────────────────────────────────────────────────────────┤
│  Layer 1: Atomic Tools (原子命令)                            │
│  servo_set, buzzer_beep, lcd_print, display_number, ...    │
├─────────────────────────────────────────────────────────────┤
│  Transport: TCP JSON → ESP32 (192.168.4.1:3333)            │
└─────────────────────────────────────────────────────────────┘
```

| 层级 | 说明 | 示例 |
|------|------|------|
| **原子工具** | 直接映射 JSON 协议的单一硬件操作 | `servo_set(90)`, `buzzer_beep(3)` |
| **语义工具** | 多设备协调的高层语义操作 | `express("happy")`, `gauge(75)` |
| **资源** | 只读参考信息，不控制硬件 | 硬件配置、协议摘要、表情列表 |

### 安全机制

| 机制 | 说明 |
|------|------|
| **参数校验** | 所有输入参数在发送前强制限制在合法范围 |
| **速率限制** | 最多 10 条命令/秒，防止 TCP 溢出 |
| **自动重连** | 连接断开时自动重试一次 |
| **状态缓存** | 记住最后已知状态，减少不必要的查询 |
| **线程安全** | TCP 连接由互斥锁保护，支持并发工具调用 |

---

## 工具参考

### 原子工具 (Layer 1)

#### 舵机 (Servo)

| 工具 | 参数 | 说明 |
|------|------|------|
| `servo_set` | `angle: 0-180` | 立即设置角度 |
| `servo_smooth` | `angle: 0-180, speed: 1-360` | 平滑移动到目标角度 |
| `servo_get` | 无 | 获取当前角度 |
| `servo_sweep` | `start, end, speed, count` | 往复扫描 |

#### 蜂鸣器 (Buzzer)

| 工具 | 参数 | 说明 |
|------|------|------|
| `buzzer_beep` | `count: 1-20` | 短促哔声 |
| `buzzer_tone` | `freq: 20-20000, ms: 1-10000` | 指定频率发声 |
| `buzzer_melody` | `name: startup/success/error` | 播放内置旋律 |

#### 数码管 (Display - TM1637)

| 工具 | 参数 | 说明 |
|------|------|------|
| `display_number` | `value: 0-9999` | 显示数字 |
| `display_text` | `value: 最多4字符` | 显示文本 (0-9, A-F, -, 空格) |
| `display_clear` | 无 | 清空显示 |

#### LCD (Grove LCD RGB 16x2)

| 工具 | 参数 | 说明 |
|------|------|------|
| `lcd_print` | `text: 最多16字符, row: 0或1` | 显示文字 |
| `lcd_rgb` | `r, g, b: 0-255` | 设置背光颜色 |
| `lcd_clear` | 无 | 清屏 |

#### 通用

| 工具 | 参数 | 说明 |
|------|------|------|
| `send_raw_json` | `json_str` | 发送原始 JSON 命令 |

### 语义工具 (Layer 2)

#### `express(emotion)`

用全部硬件协同表达情感:

| 情感 | 背光色 | LCD 表情 | 舵机 | 声音 |
|------|--------|---------|------|------|
| `happy` | 绿色 | `(* v *)` | 0° (点头) | success 旋律 |
| `sad` | 蓝色 | `(T _ T)` | 180° (低头) | 低沉音 200Hz |
| `yes` | 绿色 | `Correct!` | 0° | 双声哔 |
| `no` | 红色 | `Wrong!` | 180° | error 旋律 |
| `thinking` | 黄色 | `(o . o)?` | 70°↔110° 慢摆 | 无 |
| `alert` | 红色 | `!! ALERT !!` | 0°↔180° 快摆 | 5声哔 |
| `celebrate` | 紫色 | `\(^o^)/` | 0°↔180° 快摆 | startup 旋律 |

#### `gauge(value, label, unit)`

物理仪表盘显示:
- **舵机** = 指针 (0°=0%, 180°=100%)
- **数码管** = 数值
- **LCD** = 标签 + 进度条
- **背光** = 绿(<50) → 黄(50-80) → 红(>80)
- **蜂鸣器** = >90% 时警报

```
gauge(75, "CPU Usage", "%")
→ 舵机=135°, 数码管=75, LCD=[=========   ] 75%, 黄色背光
```

#### `reset_hardware()`

将所有硬件恢复到默认空闲状态:
- 舵机 → 90° (居中)
- LCD → 蓝色背光, "ESP32 Ready"
- 数码管 → "----"

#### `get_status()`

返回所有硬件的最后已知状态 (缓存值，不发送命令到 ESP32)。

### 资源 (Resources)

| URI | 内容 |
|-----|------|
| `esp32://hardware` | 硬件配置、GPIO 映射表 |
| `esp32://protocol` | JSON 命令协议摘要 |
| `esp32://emotions` | 可用情感表达列表 |

---

## AI Agent 使用示例

### 示例 1: 回答问题时用硬件反馈

```
用户: "2 + 2 等于几？"
Agent 思考: 答案是 4

调用: express("yes")           → 绿灯 + "Correct!" + 舵机点头
调用: display_number(4)        → 数码管显示 "4"
调用: lcd_print("2 + 2 = 4", 1) → LCD 第二行显示答案
```

### 示例 2: 系统监控仪表盘

```
Agent: 检测到 CPU 使用率 73%
调用: gauge(73, "CPU Usage", "%")
→ 舵机=131°, 数码管=73, LCD 进度条, 黄色背光
```

### 示例 3: 自由创作表演

```
Agent: 用户说"表演一个开心的舞蹈"

调用: lcd_rgb(255, 0, 0)      → 红色
调用: servo_set(0)             → 左转
调用: buzzer_tone(523, 200)    → C5
调用: lcd_rgb(0, 255, 0)      → 绿色
调用: servo_set(180)           → 右转
调用: buzzer_tone(659, 200)    → E5
调用: lcd_rgb(0, 0, 255)      → 蓝色
调用: servo_set(90)            → 居中
调用: buzzer_tone(784, 400)    → G5
调用: lcd_print("Ta-da!", 0)   → 结束
```

### 示例 4: 番茄钟

```
Agent: 设置 25 分钟番茄钟

调用: lcd_print("Pomodoro 25:00", 0)
调用: lcd_rgb(0, 255, 0)       → 绿色=工作中
调用: display_number(2500)      → 25:00
调用: servo_set(0)              → 指针起点

... 每分钟更新 ...

调用: express("celebrate")      → 完成庆祝!
```

---

## 技术细节

### 连接管理

- **懒连接**: 第一次工具调用时才建立 TCP 连接
- **自动重连**: 连接断开后自动重试一次
- **超时**: 连接超时 5s，命令超时 3s
- **线程安全**: 所有 TCP 操作由 `threading.Lock` 保护

### 状态缓存

Server 维护一个状态缓存 (`get_status()`)，记录:
- 舵机当前角度
- LCD 两行文字内容
- LCD 背光 RGB 值
- 数码管显示值
- TCP 连接状态

缓存在每次成功命令后自动更新，不额外占用通信带宽。

### 速率限制

命令发送间隔最小 100ms (10 cmd/s)，防止:
- ESP32 TCP 缓冲区溢出
- 舵机/蜂鸣器命令堆积
- 网络拥塞

### 错误处理

| 场景 | 行为 |
|------|------|
| WiFi 未连接 | 返回错误提示 "Cannot connect to ESP32" |
| ESP32 未开机 | 同上，连接超时 |
| 参数越界 | 自动裁剪到合法范围（不报错） |
| JSON 解析失败 | 返回错误描述 |
| 连接中断 | 自动重连一次，仍失败则报错 |

---

## 文件清单

```
~/Documents/esp32/
├── esp32_mcp_server.py         # MCP Server 主程序
├── MCP_SERVER.md               # 本文档
├── wifi_echo/
│   ├── PROTOCOL.md             # 底层 JSON 协议文档
│   └── wifi_client.py          # 交互式客户端 (人工使用)
└── .vscode/
    └── mcp.json                # VS Code MCP 配置 (可选)
```

---

## 与现有系统的关系

```
                           ┌─────────────────────────────────┐
                           │        ESP32 硬件                │
                           │  蜂鸣器 ┃ 舵机 ┃ 数码管 ┃ LCD   │
                           └────────────────┬────────────────┘
                                            │ TCP JSON
                                            │ 192.168.4.1:3333
                    ┌───────────────────────┼───────────────────────┐
                    │                       │                       │
            ┌───────┴────────┐    ┌────────┴────────┐    ┌────────┴────────┐
            │ wifi_client.py │    │ esp32_mcp_server │    │ cpu_monitor.py  │
            │ (人工交互)      │    │ (AI Agent 接口)  │    │ (自动监控)       │
            └────────────────┘    └────────┬────────┘    └─────────────────┘
                                           │ MCP (stdio)
                                  ┌────────┴────────┐
                                  │    AI Agent      │
                                  │ OpenClaw/Copilot │
                                  └─────────────────┘
```

| 接口 | 用户 | 协议 | 场景 |
|------|------|------|------|
| `wifi_client.py` | 人类 | TCP JSON + 快捷命令 | 交互式调试、手动控制 |
| `esp32_mcp_server.py` | AI Agent | MCP (stdio) | 自动化控制、智能交互 |
| `cpu_monitor.py` | 脚本 | TCP JSON | 系统监控仪表盘 |
| `happy_show.py` / `sad_show.py` | 脚本 | TCP JSON | 表演演示 |
