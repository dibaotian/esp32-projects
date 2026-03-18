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
- `components/grove_lcd/` — Grove LCD RGB 16x2 驱动 (I2C, 已修复 HD44780 时序)

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
| `/esp32/melody_cmd` | `std_msgs/msg/String` | 在线旋律播放: 格式 `P<间隔>\|freq,dur;freq,dur;...` ESP32 本地解析播放 |

### Topics (发布 — 状态上报)

| Topic | 消息类型 | 频率 | 说明 |
|-------|----------|------|------|
| `/esp32/heartbeat` | `std_msgs/msg/Int32` | 0.2 Hz (5s) | uptime 秒数 |
| `/esp32/servo_state` | `std_msgs/msg/Float32` | 0.2 Hz (5s) | 当前舵机角度 (随心跳一起发布) |

## FreeRTOS 任务模型

| 任务名 | 栈大小 | 优先级 | 说明 |
|--------|--------|--------|------|
| `main` (app_main) | 8192 B | 1 | 初始化外设 + 开机动画 + WiFi STA + 创建 uros 任务 |
| `uros_task` | 16384 B | 5 | micro-ROS executor 主循环 (含心跳定时器 + 4个订阅者) || `hw_worker` | 4096 B | 6 | 从队列取硬件命令执行 (优先级高于 executor, 确保即时响应) |

> **优先级设计**: hw_worker(6) > uros_task(5)，当 subscriber 回调通过 `xQueueSend` 入队后，
> hw_worker 立即抢占 uros_task 执行硬件操作，确保零延迟响应。

> **CONFIG_FREERTOS_HZ=100** (tick=10ms): `pdMS_TO_TICKS(N)` 当 N<10 时等于 0，
> 因此 grove_lcd 驱动中 HD44780 时序用 `esp_rom_delay_us()` 替代 `vTaskDelay`。
### 启动流程

```
app_main()
    │
    ├─ 1. NVS Flash 初始化
    ├─ 2. 外设初始化 (buzzer/servo/tm1637/grove_lcd)
    ├─ 3. 开机动画 (与 wifi_echo 相同)
    ├─ 4. WiFi STA 初始化 → 连接 Ubuntu 热点 (Ubuntu-ROS)
    ├─ 5. 禁用 WiFi 省电 (WIFI_PS_NONE, 保证 UDP 稳定)
    ├─ 6. xTaskCreate(micro_ros_task) → uros_task 开始 ↓
    │
    │   ── micro_ros_task() 内部 ──
    ├─ 7. 连接 micro-ROS Agent (带重试)
    ├─ 8. 等待 XRCE-DDS 会话稳定 (500ms)
    ├─ 9. 创建节点 "esp32_controller" (namespace: esp32)
    ├─ 10. 创建 2 个 publishers (每次间隔 200ms)
    ├─ 11. 创建 4 个 subscribers (每次间隔 200ms)
    ├─ 12. 创建心跳定时器 (5 秒)
    ├─ 13. 初始化 executor (5 handles = 4 sub + 1 timer)
    ├─ 14. LCD 显示 "ROS2 Connected!"
    ├─ 15. 启动 hw_worker 任务 (优先级 6, 栈 4096B)
    ├─ 16. 5秒后 LCD 切换欢迎信息
    └─ 17. rclc_executor_spin_some(20ms) + vTaskDelay(1) 无限循环
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

### 舵机扫描：从网络发送到固件本地执行的演进

#### 最终方案

`scan slow` 和 `scan fast` 命令的扫描逻辑**在 ESP32 固件内部执行**，Python 只发送一条触发命令（servo_cmd = -1.0 或 -2.0），ESP32 hw_worker 本地完成整个 0→180→0 扫描，舵机运动完全由 `vTaskDelay(20ms)` 精确控制。

| 模式 | 步进 | 每步延迟 | 触发值 | 总耗时 |
|------|------|----------|--------|--------|
| `scan fast` | 10° | 20ms | servo_cmd = -2.0 | ~0.7s |
| `scan slow` | 2° | 20ms | servo_cmd = -1.0 | ~3.6s |

> 注意：扫描期间 hw_worker 被占用（在 for 循环里），其他硬件命令（蜂鸣器/数码管/LCD）
> 会在队列中等待扫描结束后执行。扫描不影响 XRCE-DDS 会话（uros_task 独立运行）。

#### 问题排查过程

最初 scan 是在 Python 端用 for 循环逐条 publish 角度实现的。从"能用"到"平滑"经历了 4 个阶段，逐步发现硬件极限、软件瓶颈、编译缓存陷阱、以及网络抖动的根本不可消除性。

---

**第一阶段：Python 逐条发送 — 能用但粗糙**

最初只有一个 `scan` 命令，Python 端 for 循环以 10° 步进、80ms 间隔逐条 publish 角度：

```python
# 最初的 scan 实现 (Python 端)
for angle in range(0, 181, 10):     # 0, 10, 20, ..., 180
    self.servo(angle)
    time.sleep(0.08)                # 80ms 间隔
for angle in range(180, -1, -10):   # 180, 170, ..., 0
    self.servo(angle)
    time.sleep(0.08)
```

效果：能完成扫描，但步进太大（10°），肉眼可见明显的阶梯式跳动。用户希望更平滑的扫描，因此拆分出 `scan fast`（保持原样）和 `scan slow`（更小步进）。

---

**第二阶段：缩小步进后舵机跳跃 — 发现双重瓶颈**

`scan slow` 尝试以 2° 步进、20ms 间隔发送 182 条消息（0→180→0 共 182 步）。

**现象**：舵机运动不平滑，出现**不规则跳跃** — 有时连续几步正常，然后突然跳过 10°~20°，接着又恢复。跳跃的位置每次都不一样，说明不是机械问题而是消息丢失。

排查发现**两个独立的瓶颈**叠加：

**瓶颈 1：硬件极限 — 舵机 PWM 50Hz = 20ms 周期**

舵机由 LEDC PWM 驱动，频率 50Hz，即每 20ms 一个 PWM 周期。舵机内部的控制板每个 PWM 周期只读取一次脉宽来决定目标角度。这意味着：

```
时间轴:
  0ms    20ms    40ms    60ms    80ms
  |------|------|------|------|
  PWM↑   PWM↑   PWM↑   PWM↑   PWM↑  ← 舵机每 20ms 读取一次

如果 Python 以 10ms 间隔发送:
  |--cmd--|--cmd--|--cmd--|--cmd--|--cmd--|--cmd--|--cmd--|--cmd--|
  0°      2°      4°      6°      8°      10°     12°     14°
                                  ↑                       ↑
                          只有这两个被舵机实际读到, 中间的被覆盖
```

低于 20ms 的发送间隔不仅无意义，还会浪费 XRCE-DDS 缓冲区——多条消息堆积，但舵机只能跟上 50Hz 的更新速率。

**瓶颈 2：软件缓冲溢出 — XRCE-DDS `MAX_HISTORY=4`**

ESP32 端 micro-ROS 使用 XRCE-DDS 中间件，每个 subscription 的消息缓冲区大小由 `RMW_UXRCE_MAX_HISTORY` 控制，**默认值为 4**。这意味着 servo_cmd topic 最多缓冲 4 条未处理的消息，满了以后新消息**静默丢弃旧消息**（没有任何错误日志）。

ESP32 端消息消费的时间链：

| 环节 | 耗时 | 说明 |
|------|------|------|
| `rclc_executor_spin_some()` | ~1ms | 从 XRCE-DDS 缓冲取 1 条消息，调用回调 |
| subscriber 回调 `xQueueSend()` | <0.1ms | 非阻塞入队，立即返回 |
| `vTaskDelay(pdMS_TO_TICKS(1))` | 10ms | CONFIG_FREERTOS_HZ=100，tick=10ms，所以 `pdMS_TO_TICKS(1)` = 1 tick = 10ms |
| **单次循环总计** | **~11ms** | 即每 11ms 消费 1 条消息 |

WiFi UDP 的问题在于**突发抖动（burst jitter）**：Python 以均匀 20ms 间隔发送，但 WiFi 协议栈的 CSMA/CA 退避、信道竞争、以及 TCP/IP 栈的聚合，导致 ESP32 端接收到的消息是"一阵一阵"到达的——可能 100ms 内一条都收不到，然后突然同时到达 5~8 条。

```
Python 发送 (均匀):   |--20ms--|--20ms--|--20ms--|--20ms--|--20ms--|--20ms--|--20ms--|
                      msg1     msg2     msg3     msg4     msg5     msg6     msg7

WiFi 到达 (突发):     |-------- 一条都没到 --------|msg1,msg2,msg3,msg4,msg5|--msg6--|--msg7--|
                                                   ↑
                            5 条同时到达, 但 XRCE-DDS 缓冲区只有 4 格
                            msg1 被丢弃 (FIFO 淘汰最旧的), 舵机跳过 0°直接从 2°开始
```

丢弃发生在 XRCE-DDS 内部，没有任何回调或错误码通知上层——丢了就是丢了。反映到舵机上就是不规则的角度跳跃。

**三级缓冲模型**

从 Python 到舵机，消息经过三级缓冲，**任何一级溢出都会导致消息丢失**：

```
[Python ros2 publish]
        │
        ▼ WiFi UDP (不可靠, 突发抖动)
        │
┌───────┴─────────────────────┐
│  第 1 级: XRCE-DDS 缓冲区    │  MAX_HISTORY=4 (每个 subscription 独立)
│  深度: 4 条                  │  满了: 静默丢弃最旧的消息 (无日志/无回调)
│  消费速率: 1 条/11ms         │  spin_some 每次循环只取 1 条
└───────┬─────────────────────┘
        │ subscriber 回调 → xQueueSend (非阻塞)
        ▼
┌───────┴─────────────────────┐
│  第 2 级: FreeRTOS 队列       │  深度: 8 (sizeof(hw_cmd_t))
│  深度: 8 条                  │  满了: xQueueSend 返回 errQUEUE_FULL (日志可见)
│  消费速率: 取决于硬件操作      │  hw_worker 用 xQueueReceive 取
└───────┬─────────────────────┘
        │ hw_worker 取出命令
        ▼
┌───────┴─────────────────────┐
│  第 3 级: hw_worker 执行      │  逐条执行, servo_set_angle() 立即返回
│  深度: 1 (执行中)            │  舵机实际响应受 PWM 50Hz 限制
└─────────────────────────────┘
```

在 scan slow 场景下，第 1 级（XRCE-DDS）是瓶颈：缓冲区只有 4 格，WiFi 突发 5+ 条时立即溢出。第 2 级（FreeRTOS 队列深度 8）在正常场景下够用，因为 hw_worker 消费速度快于 spin_some 入队速度。第 3 级不存在排队问题。

---

**第三阶段：增大 XRCE-DDS 缓冲 — 改善但未根治**

既然缓冲区太小导致丢消息，自然想到增大缓冲区。将 `app-colcon.meta` 中 `RMW_UXRCE_MAX_HISTORY` 从 4 改为 16：

```json
{
    "names": {
        "rmw_microxrcedds": {
            "cmake-args": [
                "-DRMW_UXRCE_MAX_HISTORY=16"
            ]
        }
    }
}
```

RAM 开销估算：每个 subscription × MAX_HISTORY × 消息大小 ≈ 5 × 16 × 16B ≈ 1.3KB，对 ESP32 的 320KB SRAM 微不足道。

**编译缓存陷阱**

修改 `app-colcon.meta` 后直接 `idf.py build`，发现**完全没有效果** —— 编译产物大小不变，串口日志显示行为没有改善。

原因：micro-ROS 的构建系统使用 colcon（而非 CMake），`idf.py build` 只检查 `libmicroros.a` 是否存在。如果存在就跳过整个 micro-ROS 编译，不会去检查 `app-colcon.meta` 是否变化。

**必须手动清理 colcon 缓存**：

```bash
cd components/micro_ros_espidf_component

# 删除 colcon 编译缓存 (build/install/log) 和预编译库
rm -rf micro_ros_src/build micro_ros_src/install micro_ros_src/log libmicroros.a

# 回到项目根目录, reconfigure 触发完整重新编译
cd ../..
idf.py reconfigure    # 会重新运行 colcon build (~35秒)
idf.py build          # 链接新的 libmicroros.a
```

如果只删 `libmicroros.a` 而不删 colcon 的 `build/install/log`，colcon 会使用缓存的编译结果重新打包——`MAX_HISTORY` 还是旧值。

更隐蔽的问题：**config.h 残留副本**。编译后 `MAX_HISTORY` 的值分布在 4 份 `config.h` 中：

```
components/micro_ros_espidf_component/
├── micro_ros_src/build/.../config.h     ← colcon build 生成 (新值 16)
├── micro_ros_src/install/.../config.h   ← colcon install 复制 (新值 16)
├── include/rmw_microxrcedds_c/config.h  ← 旧的残留副本, 可能还是 4!
└── micro_ros_src/.../config.h           ← 源码包里的 (被 cmake 覆盖)
```

`idf.py build` 链接时 include path 可能优先找到 `include/` 下的旧 `config.h`。验证方法：

```bash
grep -r "RMW_UXRCE_MAX_HISTORY" components/micro_ros_espidf_component/  \
  --include="config.h" -l
# 逐个检查每个 config.h 中的值
```

如果发现残留旧值，用 `sed` 修复：

```bash
sed -i 's/#define RMW_UXRCE_MAX_HISTORY 4/#define RMW_UXRCE_MAX_HISTORY 16/' \
  components/micro_ros_espidf_component/include/rmw_microxrcedds_c/config.h
```

**增大缓冲后的效果**

配合 step=2°、delay=40ms（考虑 20ms PWM 周期 + 20ms 余量），扫描**有所改善**——跳跃频率明显降低，大部分情况下连续运动。但仍然偶尔出现小幅跳跃，尤其是 WiFi 环境拥挤时。

根本原因：WiFi UDP 的突发抖动是**不可预测**的。即使缓冲区增大到 16，当 WiFi 信道特别拥挤时（比如附近有其他 2.4GHz 设备），几百毫秒内可能 20+ 条消息同时灌入。缓冲区大小可以缓解但无法根治——因为问题的本质不是缓冲区大小，而是**网络本身的时序不确定性**。

实测数据（delay=40ms, MAX_HISTORY=16）：

| 测试环境 | 平滑度 | 丢消息 |
|----------|--------|--------|
| WiFi 空闲（深夜） | 较好，偶尔 1~2 次微跳 | ~2% |
| WiFi 正常（白天） | 能接受，每次扫描 3~5 次跳跃 | ~5% |
| WiFi 拥挤（多设备） | 明显不平滑 | ~10%+ |

---

**第四阶段：扫描逻辑下沉到 ESP32 固件 — 根治方案**

经过前三个阶段的排查，得出结论：**通过网络发送高频连续命令实现平滑控制是不可行的**。无论怎么调参数（步进、延迟、缓冲区大小），WiFi UDP 的抖动导致消息到达间隔不均匀是物理层面的限制。

解决思路的转变：不再让 Python 发 182 条消息控制每一步，而是**只发 1 条触发命令**，让 ESP32 在本地完成整个扫描。这样舵机运动完全由 FreeRTOS 的 `vTaskDelay()` 控制，零网络依赖。

**触发协议设计**

利用 servo_cmd 的值域设计了触发协议——正常角度范围是 0°~180°，因此**负值**可以作为特殊命令：

| servo_cmd 值 | 含义 |
|---|---|
| 0.0 ~ 180.0 | 普通角度控制 |
| -1.0 | 触发 scan slow（2°/20ms） |
| -2.0 | 触发 scan fast（10°/20ms） |

ESP32 固件中 hw_worker 的实现：

```c
case HW_CMD_SERVO: {
    float angle = cmd.servo_angle;
    if (angle == -1.0f || angle == -2.0f) {
        /* 本地扫描: -1=慢(2°/20ms), -2=快(10°/20ms) */
        int step = (angle == -1.0f) ? 2 : 10;
        ESP_LOGI(TAG, "舵机扫描: step=%d°", step);
        for (int a = 0; a <= 180; a += step) {
            servo_set_angle(g_servo, (float)a);
            vTaskDelay(pdMS_TO_TICKS(20));   // 精确匹配 PWM 50Hz 周期
        }
        for (int a = 180; a >= 0; a -= step) {
            servo_set_angle(g_servo, (float)a);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        servo_set_angle(g_servo, 90.0f);    // 扫描结束回中位
        ESP_LOGI(TAG, "舵机扫描完成");
    } else {
        if (angle < 0.0f)   angle = 0.0f;   // 未知负值 clamp 到 0
        if (angle > 180.0f) angle = 180.0f;
        ESP_LOGI(TAG, "舵机命令: %.1f°", angle);
        servo_set_angle(g_servo, angle);
    }
    break;
}
```

Python 端简化为：

```python
def scan(self, slow=False):
    """舵机扫描 (ESP32 本地执行, 零网络抖动)"""
    val = -1.0 if slow else -2.0
    self.servo(val)    # 只发 1 条消息
```

**Python clamp 截断 bug**

改完固件后第一次测试, `scan slow` 和 `scan fast` 都没有执行——舵机直接转到了 0° 然后不动。

排查发现 Python 端 `servo()` 方法有角度钳位（clamp）逻辑：

```python
# 之前的代码 — 所有角度都会被 clamp 到 [0, 180]
def servo(self, angle: float):
    msg = Float32()
    msg.data = max(0.0, min(180.0, angle))   # -1.0 → 0.0, -2.0 → 0.0 !!!
    self.pub_servo.publish(msg)
```

`max(0.0, min(180.0, -1.0))` = `max(0.0, -1.0)` = `0.0`，触发值 -1.0 被截断为 0.0° 普通角度！

修复：负值绕过 clamp 直接透传：

```python
# 修复后 — 负值作为触发命令直接发出
def servo(self, angle: float):
    msg = Float32()
    msg.data = angle if angle < 0.0 else max(0.0, min(180.0, angle))
    self.pub_servo.publish(msg)
    if angle < 0.0:
        print(f'  → 舵机: 扫描命令 ({angle})')
    else:
        print(f'  → 舵机: {msg.data:.1f}°')
```

修复后 scan slow/fast 立即工作，舵机运动**完全平滑**，无任何跳跃。

---

#### 端到端延迟分析

从 Python 发出命令到舵机开始转动的延迟链路：

| 环节 | 延迟 | 说明 |
|------|------|------|
| Python `publish()` → DDS 序列化 | <1ms | rclpy 内部 |
| Ubuntu DDS → XRCE-DDS Agent 转发 | ~1ms | 同主机 loopback |
| Agent → WiFi UDP → ESP32 | 2~50ms | **变化最大**，取决于 WiFi 信道状况 |
| ESP32 `spin_some()` 取消息 | 0~11ms | 取决于在循环哪个位置 |
| subscriber 回调 `xQueueSend()` | <0.1ms | 非阻塞 |
| hw_worker `xQueueReceive()` + `servo_set_angle()` | <1ms | 优先级 6 立即抢占 |
| 舵机 PWM 生效 | 0~20ms | 等待下一个 PWM 周期 |
| **单条命令总延迟** | **5~80ms** | 平均 ~20ms |
| **scan slow 本地扫描** | **精确 20ms/步** | vTaskDelay 由 FreeRTOS tick 保证 |

关键对比：单条命令的 5~80ms 网络延迟**不影响响应感**（人感知阈值 ~100ms），但 182 条连续命令的延迟累积和抖动导致扫描不平滑。下沉到固件后，每步的 20ms 间隔由 FreeRTOS 的 tick 中断保证（CONFIG_FREERTOS_HZ=100, 1 tick = 10ms, `pdMS_TO_TICKS(20)` = 2 ticks），精度为 ±10ms，远优于 WiFi 的 ±50ms 抖动。

#### 关键教训

| 教训 | 说明 |
|------|------|
| 实时控制不应走网络 | 高频连续控制（扫描）应在设备端本地执行，网络只发触发命令。这是嵌入式系统的通用原则——控制回路尽量短 |
| 消息丢弃是静默的 | XRCE-DDS 缓冲区满时丢弃旧消息，没有任何错误日志或回调。排查时需要用计数器确认消息实际到达数量 |
| MAX_HISTORY 默认值太小 | 默认值 4 对偶发单条命令够用，但**任何突发流量场景**都需要增大。建议至少设为 16 |
| colcon 缓存不受 idf.py 管理 | `app-colcon.meta` 变更后必须手动删除 `libmicroros.a` + colcon build/install/log 目录，否则不会重新编译 |
| config.h 可能有多份 | 清理重建后检查所有 config.h 副本，确保值一致。include/ 目录下的副本可能是残留的旧版 |
| Python clamp 会截断触发值 | `max(0, min(180, angle))` 会把负数触发值截断为 0。使用负值作为特殊命令时需绕过 clamp |
| vTaskDelay(20ms) 匹配 PWM | 舵机 50Hz PWM 周期 = 20ms，这是步进间隔的物理下限。更快的 delay 浪费 CPU 且无额外效果 |
| WiFi UDP 抖动无法消除 | 即使增大缓冲、调整发送间隔，WiFi 协议层的 CSMA/CA 退避和信道争用导致的延迟抖动是不可控的 |

### 蜂鸣器音乐播放：从逐音符发送到在线旋律协议的演进

#### 最终方案

旋律数据存储在 Python 端（`esp32_ctrl.py` 的 `MELODIES` 字典），通过 `/esp32/melody_cmd` String topic **一次性发送所有音符**到 ESP32。ESP32 在 hw_worker 中解析并本地播放，时序由 `vTaskDelay()` 精确控制，零网络抖动。

```
发送格式: "P<pause_ms>|freq1,dur1;freq2,dur2;freq3,dur3;..."

示例 (Intel 开机音):  "P0|988,180;0,60;1319,180;0,60;988,180;0,60;659,180;0,60;784,400"
示例 (小星星开头):    "P30|262,300;262,300;392,300;392,300;440,300;440,300;392,500"
```

| 参数 | 说明 |
|------|------|
| `P<N>` | 音符间隔 (ms)，N=0 表示音符紧密相连 |
| `freq` | 频率 (Hz)，0=静音（休止符），20~20000 有效 |
| `dur` | 持续时间 (ms) |
| `;` | 音符分隔符 |

ESP32 端使用 1024 字节静态缓冲区，`strtok_r` + `sscanf` 解析，复用 `buzzer_tone_ms()` 逐音符播放。

#### 问题排查过程

最初尝试在 Python 端逐个发送音符频率到蜂鸣器，遇到了与舵机扫描完全相同的 WiFi 抖动问题。经历了 3 个阶段，最终找到了兼顾灵活性和音质的方案。

---

**第一阶段：Python 逐音符发送 — 不可用**

最初方案是在 Python 端用循环逐个发布 `buzzer_cmd` 音符频率：

```python
# 最初的尝试 — 逐音符通过网络发送
for freq, duration in song_notes:
    ctrl.buzzer(freq)           # 发布 buzzer_cmd (Int32)
    time.sleep(duration / 1000) # Python 端等待
    ctrl.buzzer(0)              # 停止
    time.sleep(0.03)            # 音符间隔
```

**问题**：与舵机扫描完全相同 — WiFi UDP 的突发抖动导致音符到达间隔不均匀。
表现为：
- 有些音符延迟到达，前后两个音符粘连
- 有些音符因 XRCE-DDS 缓冲溢出而丢失，直接跳过
- 整体节奏忽快忽慢，完全无法辨认旋律

根本原因与舵机扫描一致 — 高频连续命令 + WiFi UDP 抖动 = 时序不可控：

```
Python 发送 (均匀):   |--300ms--|--300ms--|--300ms--|--300ms--|
                      C4        C4        G4        G4

WiFi 到达 (突发):     |-- 400ms 空白 --|C4,C4,G4|---G4---|
                                        ↑
                             3个音符同时到达, 音长全乱了
```

这个问题用户反馈："和舵机遇到的问题一样，有些声音要等很长时间，可能有的音符没有了"。

---

**第二阶段：旋律固化到固件 — 可用但不灵活**

借鉴舵机扫描的解决思路：将旋律数据编译进固件，Python 只发一条触发命令。

在 `buzzer.c` 中直接定义了完整旋律数组：

```c
// buzzer.c — 将旋律编译到固件中
esp_err_t buzzer_play_twinkle(void) {
    static const buzzer_tone_t melody[] = {
        { NOTE_C4, 300 }, { NOTE_C4, 300 }, { NOTE_G4, 300 }, ...
    };
    return buzzer_play_melody(melody, sizeof(melody)/sizeof(melody[0]), 30);
}
```

`buzzer_cmd` 的负值作为触发码：

| buzzer_cmd 值 | 旋律 |
|---|---|
| -1 | 开机音效 (短) |
| -2 | 成功音效 (短) |
| -3 | 错误音效 (短) |
| -4 | 小星星 (~14s) |
| -5 | 生日快乐 (~12s) |
| -6 | 铃儿响叮当 (~10s) |
| -7 | 超级马里奥 (~6s) |
| -8 | 义勇军进行曲 (~25s) |

**效果**：播放完全流畅，每个音符时序精确，音乐可辨认。

**问题**：每新增一首歌都要修改 C 代码 → 重新编译 → 重新烧录。对于想「听一下什么歌」的场景极不方便。用户反馈："能不能不要先编译好，能不能先把数据在线一次全给你，然后你在 ESP32 上播放"。

---

**第三阶段：在线旋律协议 — 最终方案**

核心思路：**一次发送所有音符数据**，ESP32 先接收完整数据再本地播放。不同于第一阶段的逐音符发送，这里只有 1 条 ROS 消息，所以不存在网络抖动问题。

新增 `/esp32/melody_cmd` String topic（RELIABLE QoS），消息格式：

```
P<pause_ms>|freq1,dur1;freq2,dur2;...
```

**ESP32 固件实现**：

```c
// main.c — melody_cmd 回调
static char g_melody_data[1024];  // 静态缓冲, 避免 malloc
static volatile bool g_melody_pending = false;

static void melody_cmd_callback(const void *msgin) {
    const std_msgs__msg__String *msg = (const std_msgs__msg__String *)msgin;
    if (msg->data.size > 0 && msg->data.size < sizeof(g_melody_data)) {
        memcpy(g_melody_data, msg->data.data, msg->data.size);
        g_melody_data[msg->data.size] = '\0';
        g_melody_pending = true;
        // 入队 HW_CMD_MELODY 让 hw_worker 处理
    }
}

// hw_worker 中解析并播放
case HW_CMD_MELODY: {
    if (!g_melody_pending) break;
    g_melody_pending = false;
    char *data = g_melody_data;
    uint32_t pause_ms = 30;  // 默认间隔

    // 解析 "P30|..." 前缀
    if (data[0] == 'P') {
        char *pipe = strchr(data, '|');
        if (pipe) {
            *pipe = '\0';
            pause_ms = (uint32_t)atoi(data + 1);
            data = pipe + 1;
        }
    }

    // 逐音符解析 "freq,dur;freq,dur;..."
    char *saveptr;
    char *token = strtok_r(data, ";", &saveptr);
    while (token) {
        uint32_t freq = 0, dur = 0;
        if (sscanf(token, "%lu,%lu", &freq, &dur) == 2 && dur > 0) {
            buzzer_tone_ms(freq, dur);
            if (pause_ms > 0) vTaskDelay(pdMS_TO_TICKS(pause_ms));
        }
        token = strtok_r(NULL, ";", &saveptr);
    }
    break;
}
```

**XRCE-DDS 消息大小**：

旋律消息可能较长（义勇军进行曲约 651 字节），但 XRCE-DDS 的 UDP MTU 为 512 字节。XRCE-DDS 自动将超过 MTU 的 RELIABLE 消息拆分为多个 UDP 包并在 ESP32 端重组，应用层无需关心。1024 字节缓冲区足以容纳现有所有旋律。

**添加新订阅的代价**：

`melody_cmd` 是第 5 个 subscriber，使 XRCE-DDS 实体接近上限：

```
app-colcon.meta 配置:  MAX_SUBSCRIPTIONS=5 (刚好用完)
executor handles:      6 (5 subscribers + 1 timer)
```

如果未来需要更多 subscriber，须同步修改 `app-colcon.meta` 并清理 colcon 缓存重新编译。

---

**第四阶段：旋律数据从固件迁回 Python**

在线旋律协议稳定后，固件中的硬编码旋律（twinkle/birthday/jingle/mario/anthem）不再需要：

- 删除 `buzzer.c` 中 5 个旋律函数 → 减小 ~2KB 固件
- 删除 `buzzer.h` 中对应声明
- 删除 `main.c` 中 `-4`~`-8` 触发 handler
- 保留 `-1`(startup)、`-2`(success)、`-3`(error) 短音效（开机/操作反馈用）
- 旋律数据移至 `esp32_ctrl.py` 的 `MELODIES` 字典

```python
# esp32_ctrl.py — 旋律存储在 Python, 通过 melody_cmd 发送
MELODIES = {
    'twinkle': {
        'name': '小星星 ⭐', 'pause': 30,
        'notes': [
            (262,300),(262,300),(392,300),(392,300),(440,300),(440,300),(392,500),
            (349,300),(349,300),(330,300),(330,300),(294,300),(294,300),(262,500),
            ...
        ],
    },
    'birthday': { ... },
    'jingle':   { ... },
    'mario':    { ... },
    'anthem':   { ... },
    'intel':    { ... },
}
```

新增歌曲只需在 `MELODIES` 字典中添加条目，无需重新编译固件。

---

**第五阶段：在线调参工具 — 让旋律更像**

实际测试中发现蜂鸣器播放的旋律「不太像」原曲，原因：

1. **音高**：蜂鸣器在某些频段响度不均匀，高音区过尖、低音区偏弱
2. **节奏**：简谱转换时音符时值可能不准确，需要微调速度
3. **间隔**：音符间隔影响连贯性，过大则断断续续，过小则糊成一片

因此设计了 `melody_to_cmd_tuned()` 调参函数，支持三个维度实时调整：

```python
def melody_to_cmd_tuned(name, pitch=0, tempo=1.0, pause=-1):
    """
    pitch: 移调半音数 (+12=升一个八度, -12=降一个八度)
           频率变换: freq_new = freq × 2^(pitch/12)
    tempo: 速度倍率 (>1加快, <1减慢)
           时值变换: dur_new = dur / tempo
    pause: 音符间隔 ms (-1=使用原始值)
    """
```

在 `esp32_ctrl.py` 中通过 `tune` 命令交互式使用：

```
esp32> tune anthem                    # 原始播放
esp32> tune anthem p=-2               # 降2个半音 (更沉稳)
esp32> tune anthem t=0.85             # 慢15% (更庄重)
esp32> tune anthem p=-2 t=0.85 g=20  # 组合调参
```

调参原理：

| 参数 | 变换公式 | 效果 |
|------|----------|------|
| `p=N` (pitch) | `freq × 2^(N/12)` | 半音移调。+12=升八度，-12=降八度。蜂鸣器在 200~2000Hz 效果最佳 |
| `t=N` (tempo) | `dur / N` | 速度缩放。0.8=慢20%，1.2=快20%。不改变音高 |
| `g=N` (gap) | 直接替换 pause_ms | 音符连贯性。0=连奏，30=适中，50+=断奏 |

---

#### 方案演进对比

| 阶段 | 方案 | 消息数 | 音质 | 灵活性 | 问题 |
|------|------|--------|------|--------|------|
| ① Python 逐音符 | buzzer_cmd × N | 每音符1条 | ❌ 不可用 | ✅ 灵活 | WiFi 抖动导致节奏混乱 |
| ② 固件硬编码 | buzzer_cmd × 1 | 触发1条 | ✅ 完美 | ❌ 需重编译 | 新歌必须改 C 代码+烧录 |
| ③ 在线旋律协议 | melody_cmd × 1 | 1条 String | ✅ 完美 | ✅ 灵活 | — |
| ④ 旋律回迁 Python | melody_cmd × 1 | 1条 String | ✅ 完美 | ✅✅ 极佳 | — |

#### 关键教训

| 教训 | 说明 |
|------|------|
| 时序敏感操作必须本地执行 | 与舵机扫描同理：音乐播放对时序要求极高（人耳对节奏偏差的感知阈值 ~20ms），网络抖动不可接受 |
| "一次发送 + 本地解析"是通用模式 | 舵机扫描发 1 条触发命令，旋律发 1 条完整数据——核心思想相同：减少网络交互次数，将实时控制下沉到设备端 |
| 固件硬编码不是好的灵活性方案 | 虽然解决了时序问题，但每次改歌要重编译。在线协议同时解决了时序和灵活性 |
| String topic 可传递复杂数据 | 利用 String 类型配合自定义文本协议（`P30\|freq,dur;...`），避免了定义自定义 ROS 消息类型的复杂度 |
| 1024 字节缓冲足够 | 静态分配避免 malloc 碎片。最长旋律（义勇军进行曲 ~651 字节）有充足余量。XRCE-DDS 自动分片重组 |
| 调参工具必不可少 | 简谱 → 频率/时值的转换往往不完美，需要实时微调音高、速度、间隔才能让蜂鸣器播放出辨识度高的旋律 |

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

> **注意**: `ros2 topic pub --once` 每次都要做 DDS 发现, 有 2-3 秒延迟。
> 推荐使用下面的交互式控制器。

### 快速控制: esp32_ctrl.py (推荐)

`esp32_ctrl.py` 是持久连接的交互式 Python 控制器, 跳过 DDS 发现开销, 命令延迟 <100ms:

```bash
source /opt/ros/jazzy/setup.bash
cd ~/Documents/esp32/wifi_echo_micro_ros
python3 esp32_ctrl.py
```

交互示例:

```
ESP32 micro-ROS 控制器
等待 DDS 发现 ESP32 节点... 已就绪!
输入 help 查看命令

esp32> servo 90
  → 舵机: 90.0°
esp32> buzzer 3
  → 蜂鸣器: beep × 3
esp32> display 2026
  → 数码管: 2026
esp32> lcd Hello ROS2!
  → LCD: Hello ROS2!
esp32> scan
  → 舵机: 0.0°
  → 舵机: 10.0°
  ...
esp32> demo
--- 演示开始 ---
...
--- 演示结束 ---
esp32> status
  心跳: 120s
  舵机: 90.0°
esp32> quit
已退出
```

完整命令列表:

| 命令 | 说明 | 示例 |
|------|------|------|
| `servo <角度>` | 舵机 0~180° | `servo 90` |
| `buzzer <次数>` | 蜂鸣器响 N 次 | `buzzer 3` |
| `tone <频率>` | 播放频率 Hz | `tone 1000` |
| `buzzer stop/startup/success/error` | 蜂鸣器控制 | `buzzer success` |
| `display <数字>` | 数码管 0~9999 | `display 2026` |
| `display off` | 数码管清屏 | |
| `lcd <文字>` | LCD 显示文字 | `lcd Hello` |
| `scan` | 舵机扫描 0→180→0 | |
| `demo` | 完整演示序列 | |
| `status` | 查看心跳和舵机角度 | |
| `quit` | 退出 | |

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
├── sdkconfig                   # 实际编译配置 (100Hz tick 等)
├── app-colcon.meta             # XRCE-DDS 实体限制配置
├── esp32_ctrl.py               # 交互式 Python 控制器 (推荐)
├── esp32_ros_mcp_server.py     # ROS 2 MCP Server (AI Agent 用)
├── main/
│   ├── CMakeLists.txt          # 组件依赖声明
│   ├── Kconfig.projbuild       # 栈大小/优先级配置
│   └── main.c                  # micro-ROS 节点 + hw_worker 任务
└── components/
    ├── buzzer/                 # 蜂鸣器驱动
    ├── servo/                  # 舵机驱动
    ├── tm1637/                 # TM1637 数码管驱动
    ├── grove_lcd/              # Grove LCD RGB 驱动 (已修复 HD44780 时序)
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

# 方式 1: 交互式控制器 (推荐, 延迟 <100ms)
source /opt/ros/jazzy/setup.bash
python3 esp32_ctrl.py

# 方式 2: ros2 命令行 (每次 ~2-3 秒 DDS 发现)
source /opt/ros/jazzy/setup.bash
ros2 node list
ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 90.0}"
```

## AI Agent 控制 (MCP Server)

大模型（Claude、Copilot 等）可通过 **MCP (Model Context Protocol)** 直接控制 ESP32 硬件。

### 架构对比

本项目有两条控制路径，分别对应两个固件版本：

```
方式一: TCP/JSON (wifi_echo 固件, 旧版)
  AI Agent → MCP (stdio) → esp32_mcp_server.py → TCP/JSON → ESP32 (AP 192.168.4.1:3333)

方式二: ROS 2 (wifi_echo_micro_ros 固件, 本项目)
  AI Agent → MCP (stdio) → esp32_ros_mcp_server.py → rclpy → DDS → Agent → ESP32
```

| 对比 | TCP/JSON MCP Server | ROS 2 MCP Server |
|------|---------------------|-------------------|
| 文件 | `../esp32_mcp_server.py` | `esp32_ros_mcp_server.py` |
| 固件 | wifi_echo | wifi_echo_micro_ros (本项目) |
| 传输 | TCP 直连 ESP32 | DDS → micro-ROS Agent → UDP |
| 依赖 | `pip install mcp` | `pip install mcp` + ROS 2 |
| 延迟 | ~50ms | ~20ms (持久 DDS 连接) |
| 优势 | 简单、无需 Agent | 标准 ROS 2 生态、可与其他节点协同 |

### 配置 MCP Server

#### 前提条件

```bash
# 安装 MCP 库
pip3 install mcp

# 确保 micro-ROS Agent 运行中
docker run -d --rm --name micro-ros-agent --net=host \
  microros/micro-ros-agent:jazzy udp4 --port 8888 -v6

# ESP32 已上线
source /opt/ros/jazzy/setup.bash
ros2 node list   # 应看到 /esp32/esp32_controller
```

#### VS Code / GitHub Copilot

在 `.vscode/mcp.json` 中添加：

```json
{
  "servers": {
    "esp32_ros": {
      "command": "bash",
      "args": ["-c", "source /opt/ros/jazzy/setup.bash && python3 /home/xilinx/Documents/esp32/wifi_echo_micro_ros/esp32_ros_mcp_server.py"]
    }
  }
}
```

> **注意**: 必须通过 `bash -c` 启动，先 source ROS 2 环境再运行 Python。
> 直接用 `python3` 启动会因找不到 rclpy 而失败。

#### Claude Desktop

在 `~/.config/claude/claude_desktop_config.json` 中添加：

```json
{
  "mcpServers": {
    "esp32_ros": {
      "command": "bash",
      "args": ["-c", "source /opt/ros/jazzy/setup.bash && python3 /home/xilinx/Documents/esp32/wifi_echo_micro_ros/esp32_ros_mcp_server.py"]
    }
  }
}
```

### MCP 工具列表

配置好 MCP Server 后，AI Agent 可直接调用以下工具：

#### 原子工具

| 工具 | 参数 | 说明 |
|------|------|------|
| `servo_set` | `angle: 0-180` | 设置舵机角度 |
| `servo_scan` | `mode: fast/slow` | 触发 ESP32 本地扫描 |
| `buzzer_beep` | `count: 1-10` | 发出蜂鸣声 |
| `buzzer_tone` | `freq: 21-20000` | 播放指定频率 |
| `buzzer_melody` | `name: startup/success/error` | 播放内置旋律 |
| `buzzer_stop` | 无 | 停止蜂鸣器 |
| `display_number` | `value: 0-9999, -1=清屏` | 数码管显示数字 |
| `lcd_print` | `text: 字符串` | LCD 显示文字 |
| `get_status` | 无 | 查看心跳和舵机角度 |

#### 语义工具

| 工具 | 参数 | 说明 |
|------|------|------|
| `express` | `emotion: happy/sad/thinking/yes/no/alert/celebrate` | 全硬件协同表达情感 |
| `reset_hardware` | 无 | 所有硬件恢复空闲状态 |

### AI Agent 使用示例

当 MCP Server 配置好后，AI Agent 可以这样控制硬件：

```
用户: "把舵机转到 45 度，然后在 LCD 上显示 Hello"
Agent: [调用 servo_set(angle=45)] → [OK] Servo → 45.0°
       [调用 lcd_print(text="Hello")] → [OK] LCD → "Hello"

用户: "你觉得今天天气怎么样？开心点！"
Agent: [调用 express(emotion="happy")] → [OK] Expressing: happy (smile, success melody, nod)

用户: "ESP32 还在线吗？"
Agent: [调用 get_status()] → [OK] uptime=350s, servo=45.0°
```

### 不用 MCP 也能控制

如果 AI Agent 有终端访问权限（如 Copilot in VS Code），也可直接运行 ROS 2 命令：

```bash
# 方式一: ros2 命令行 (简单但每次 2-3 秒 DDS 发现)
source /opt/ros/jazzy/setup.bash
ros2 topic pub --once /esp32/servo_cmd std_msgs/msg/Float32 "{data: 90.0}"
ros2 topic pub --once /esp32/buzzer_cmd std_msgs/msg/Int32 "{data: 3}"

# 方式二: 交互式控制器 (持久连接, <100ms 延迟)
python3 esp32_ctrl.py
# 然后输入: servo 90, buzzer 3, scan fast, ...

# 方式三: 一次性 Python 脚本
python3 -c "
import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32
rclpy.init()
node = Node('tmp')
pub = node.create_publisher(Float32, '/esp32/servo_cmd', 10)
import time; time.sleep(2)  # DDS discovery
msg = Float32(); msg.data = 90.0; pub.publish(msg)
node.destroy_node(); rclpy.shutdown()
"
```

MCP Server 的优势是：**低延迟**（持久连接，跳过 DDS 发现）、**类型安全**（参数有描述和范围约束）、**自描述**（AI 自动理解可用工具和参数含义）。

## 后续扩展

- 添加 `/esp32/system_info` Service (`std_srvs/srv/Trigger`) 返回系统信息
- 添加 IMU 传感器 → 发布 `sensor_msgs/msg/Imu`
- 添加电机编码器 → 发布 `nav_msgs/msg/Odometry`
- 接入 Nav2 导航栈
- 接入 SLAM (cartographer / slam_toolbox)
- 多 ESP32 节点组网
