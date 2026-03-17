# ESP32 控制 - Agent & Skill 使用指南

## 概述

本项目提供两种方式让 AI Agent 控制 ESP32 硬件：

| 方式 | 文件 | 用途 |
|------|------|------|
| **Skill** | `.github/skills/esp32-control/SKILL.md` | 任何 agent 按需调用的"工具包" |
| **Agent** | `.github/agents/esp32.agent.md` | 专用的 ESP32 控制"专家角色" |

---

## 快速开始

### 1. 硬件准备

确保 ESP32 开发板已烧录 `wifi_echo` 固件并上电。

外设接线表：

| 外设 | GPIO | 备注 |
|------|------|------|
| 舵机 (180°) | 27 | PWM 信号线 |
| 蜂鸣器 (被动) | 25 | PWM 驱动 |
| TM1637 CLK | 16 | 数码管时钟 |
| TM1637 DIO | 17 | 数码管数据 |
| TM1637 VCC | 5V | 必须 5V 供电 |

### 2. 网络连接

在使用前，需要让主机连接到 ESP32 的 WiFi：

```bash
bash ~/Documents/esp32/wifi_echo/setup_dual_network.sh
```

这会连接 WiFi `ESP32-Control`（密码 `esp32ctrl`），同时保持有线网络。

### 3. 验证连接

```bash
ping 192.168.4.1
```

---

## 方式一：使用 Skill（推荐）

### 什么是 Skill

Skill 是一个"知识包"，任何 Copilot agent 在需要控制 ESP32 时会自动加载它。你不需要手动切换模式。

### 如何触发

在 Copilot Chat 中直接说：

```
控制舵机转到 90 度
让蜂鸣器响三下
数码管显示 1234
做一次舵机自检
```

或者用斜杠命令显式调用：

```
/esp32-control 舵机扫描 0 到 180 度
```

Copilot 会自动加载 Skill 中的协议知识，生成正确的 Python 控制代码并执行。

### Skill 文件结构

```
.github/skills/esp32-control/
├── SKILL.md              # 主文件：协议、命令表、代码模板
├── scripts/
│   └── esp32_cmd.py      # 命令行控制脚本
└── references/
    └── PROTOCOL.md       # 完整协议文档（链接）
```

### 使用 esp32_cmd.py 脚本

Skill 包含一个独立的命令行脚本，可以直接发送命令：

```bash
# 单条命令
python3 .github/skills/esp32-control/scripts/esp32_cmd.py \
  '{"cmd":"servo","act":"set","angle":90}'

# 多条命令
python3 .github/skills/esp32-control/scripts/esp32_cmd.py \
  '{"cmd":"servo","act":"set","angle":0}' \
  '{"cmd":"display","act":"number","value":0}'
```

---

## 方式二：使用 Agent

### 什么是 Agent

Agent 是一个"专家角色"，切换到该模式后，它始终以 ESP32 控制为核心上下文。适合长时间的硬件交互会话。

### 如何使用

在 VS Code Copilot Chat 中：

1. 点击聊天输入框左侧的 Agent 选择器（或输入 `@`）
2. 选择 **esp32**
3. 开始对话：

```
@esp32 做一次舵机自检
@esp32 扫描 0-180 度，数码管同步显示
@esp32 蜂鸣器播放开机音乐
```

### Agent vs Skill 区别

| 特性 | Skill | Agent |
|------|-------|-------|
| 触发方式 | 自动识别/斜杠命令 | 手动选择 `@esp32` |
| 上下文 | 按需加载 | 始终包含 ESP32 知识 |
| 工具权限 | 继承当前 agent | 独立配置 (execute, read, search) |
| 适用场景 | 偶尔控制硬件 | 专注硬件交互会话 |

---

## 完整命令参考

### 舵机 (servo)

```json
{"cmd":"servo", "act":"set",    "angle": 90}
{"cmd":"servo", "act":"get"}
{"cmd":"servo", "act":"smooth", "angle": 180, "speed": 60}
{"cmd":"servo", "act":"sweep",  "start": 0, "end": 180, "speed": 45, "count": 1}
{"cmd":"servo", "act":"pulse",  "us": 1500}
{"cmd":"servo", "act":"stop"}
```

### 蜂鸣器 (buzzer)

```json
{"cmd":"buzzer", "act":"beep",   "count": 3}
{"cmd":"buzzer", "act":"tone",   "freq": 1000, "ms": 500}
{"cmd":"buzzer", "act":"melody", "name": "startup"}
{"cmd":"buzzer", "act":"volume", "value": 30}
{"cmd":"buzzer", "act":"stop"}
```

### 数码管 (display)

```json
{"cmd":"display", "act":"number", "value": 1234}
{"cmd":"display", "act":"text",   "value": "AbCd"}
{"cmd":"display", "act":"raw",    "segs": [63, 6, 91, 79]}
{"cmd":"display", "act":"bright", "value": 7}
{"cmd":"display", "act":"colon",  "on": true}
{"cmd":"display", "act":"clear"}
```

### 系统 (sys)

```json
{"cmd":"sys", "act":"help"}
```

---

## 响应格式

所有 JSON 命令的响应格式：

```json
{
  "status": "ok",      // "ok" 或 "error"
  "cmd": "servo",      // 回显模块名
  "act": "set",        // 回显动作名
  "msg": "angle=90.0"  // 可读描述
}
```

---

## 示例场景

### 1. 舵机自检

```
帮我做一次舵机自检：0°→90°→180°→90°→扫描→回零
```

### 2. 角度同步显示

```
舵机从 0 扫到 90 度，数码管同步显示角度
```

### 3. 问答游戏

```
准备一个问答游戏：正确点头(0°)，错误摇头(180°)，不确定摇摆
```

### 4. 状态指示

```
数码管显示当前舵机角度，蜂鸣器响一下确认
```

---

## 故障排查

| 问题 | 解决方案 |
|------|---------|
| 无法连接 | 运行 `setup_dual_network.sh` 重新连接 WiFi |
| 连接中断 | ESP32 可能重启，等待 5 秒后重试 |
| 舵机不动 | 确认 GPIO 27 接线，检查供电 |
| 数码管不亮 | TM1637 必须用 5V 供电 |
| 命令无响应 | 检查消息是否以 `\n` 结尾 |

---

## 文件索引

| 文件 | 说明 |
|------|------|
| `.github/skills/esp32-control/SKILL.md` | Skill 主文件 |
| `.github/skills/esp32-control/scripts/esp32_cmd.py` | 命令行控制脚本 |
| `.github/agents/esp32.agent.md` | ESP32 Agent 定义 |
| `wifi_echo/PROTOCOL.md` | 完整通信协议文档 |
| `wifi_echo/wifi_client.py` | Python 交互式客户端 |
| `wifi_echo/setup_dual_network.sh` | WiFi 连接脚本 |
| `wifi_echo/components/servo/README.md` | 舵机驱动 API 文档 |
| `wifi_echo/components/buzzer/README.md` | 蜂鸣器驱动 API 文档 |
| `wifi_echo/components/tm1637/README.md` | 数码管驱动 API 文档 |
