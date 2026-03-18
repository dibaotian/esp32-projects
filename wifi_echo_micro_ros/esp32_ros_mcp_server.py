#!/usr/bin/env python3
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

"""
ESP32 ROS 2 MCP Server — AI Agent 通过 MCP 协议控制 ESP32 硬件

架构:
  AI Agent → MCP Protocol (stdio) → 本 Server → rclpy → DDS
    → micro-ROS Agent (Docker) → WiFi UDP → ESP32 (micro-ROS)

与 esp32_mcp_server.py (TCP/JSON 版) 的区别:
  - TCP/JSON 版对接 wifi_echo 固件 (ESP32 做 AP, 192.168.4.1:3333)
  - 本 ROS 2 版对接 wifi_echo_micro_ros 固件 (Ubuntu 做 AP, DDS 通信)

前提条件:
  1. source /opt/ros/jazzy/setup.bash
  2. micro-ROS Agent 运行中:
     docker run --rm --net=host microros/micro-ros-agent:jazzy udp4 --port 8888
  3. ESP32 已连接 WiFi 并上线 (ros2 node list 可见 /esp32/esp32_controller)
  4. pip3 install mcp

用法:
  # 直接运行测试
  python3 esp32_ros_mcp_server.py

  # VS Code / Copilot 配置 (.vscode/mcp.json)
  {
    "servers": {
      "esp32_ros": {
        "command": "bash",
        "args": ["-c", "source /opt/ros/jazzy/setup.bash && python3 /home/xilinx/Documents/esp32/wifi_echo_micro_ros/esp32_ros_mcp_server.py"]
      }
    }
  }

  # Claude Desktop (~/.config/claude/claude_desktop_config.json)
  {
    "mcpServers": {
      "esp32_ros": {
        "command": "bash",
        "args": ["-c", "source /opt/ros/jazzy/setup.bash && python3 /home/xilinx/Documents/esp32/wifi_echo_micro_ros/esp32_ros_mcp_server.py"]
      }
    }
  }
"""

import atexit
import threading
import time
from typing import Optional

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from std_msgs.msg import Float32, Int32, String

from mcp.server.fastmcp import FastMCP

# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------
mcp = FastMCP(
    "esp32_ros",
    instructions=(
        "ESP32 hardware controller via ROS 2 micro-ROS. "
        "Controls: servo motor (0-180°), buzzer (beep/tone/melody), "
        "TM1637 4-digit 7-segment display (0-9999), "
        "Grove LCD RGB 16x2 character display. "
        "The ESP32 must be connected via micro-ROS Agent. "
        "Use get_status() to check if ESP32 is alive (heartbeat)."
    ),
)


# ---------------------------------------------------------------------------
# ROS 2 Bridge Node
# ---------------------------------------------------------------------------
class ESP32ROSBridge(Node):
    """ROS 2 node bridging MCP tool calls to ESP32 micro-ROS topics."""

    def __init__(self):
        super().__init__('esp32_mcp_bridge')

        # QoS: RELIABLE for commands (match micro-ROS subscribers)
        pub_qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )
        # QoS: BEST_EFFORT for status (match micro-ROS publishers)
        sub_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
        )

        # Publishers — 命令下发
        self.pub_servo = self.create_publisher(
            Float32, '/esp32/servo_cmd', pub_qos)
        self.pub_buzzer = self.create_publisher(
            Int32, '/esp32/buzzer_cmd', pub_qos)
        self.pub_display = self.create_publisher(
            Int32, '/esp32/display_cmd', pub_qos)
        self.pub_lcd = self.create_publisher(
            String, '/esp32/lcd_cmd', pub_qos)
        self.pub_melody = self.create_publisher(
            String, '/esp32/melody_cmd', pub_qos)

        # Subscribers — 状态上报
        self._heartbeat: Optional[int] = None
        self._servo_state: Optional[float] = None
        self._lock = threading.Lock()

        self.create_subscription(
            Int32, '/esp32/heartbeat', self._on_heartbeat, sub_qos)
        self.create_subscription(
            Float32, '/esp32/servo_state', self._on_servo_state, sub_qos)

    def _on_heartbeat(self, msg):
        with self._lock:
            self._heartbeat = msg.data

    def _on_servo_state(self, msg):
        with self._lock:
            self._servo_state = msg.data

    @property
    def heartbeat(self) -> Optional[int]:
        with self._lock:
            return self._heartbeat

    @property
    def servo_angle(self) -> Optional[float]:
        with self._lock:
            return self._servo_state

    def wait_ready(self, timeout: float = 5.0) -> bool:
        """等待 DDS 发现 ESP32 subscribers."""
        pubs = [self.pub_servo, self.pub_buzzer, self.pub_display, self.pub_lcd, self.pub_melody]
        t0 = time.time()
        while time.time() - t0 < timeout:
            if all(p.get_subscription_count() > 0 for p in pubs):
                return True
            time.sleep(0.1)
        return False


# ---------------------------------------------------------------------------
# Lazy-init singleton
# ---------------------------------------------------------------------------
_bridge: Optional[ESP32ROSBridge] = None
_spin_thread: Optional[threading.Thread] = None


def _ensure_bridge() -> ESP32ROSBridge:
    """Lazy-init ROS 2 node + background spin thread."""
    global _bridge, _spin_thread
    if _bridge is not None:
        return _bridge

    rclpy.init()
    _bridge = ESP32ROSBridge()

    def _spin():
        while rclpy.ok():
            try:
                rclpy.spin_once(_bridge, timeout_sec=0.05)
            except Exception:
                break

    _spin_thread = threading.Thread(target=_spin, daemon=True)
    _spin_thread.start()

    # DDS discovery 需要 ~1-2 秒
    _bridge.wait_ready(timeout=5.0)
    return _bridge


def _cleanup():
    global _bridge
    if _bridge is not None:
        _bridge.destroy_node()
        rclpy.shutdown()
        _bridge = None


atexit.register(_cleanup)


# ===========================================================================
# MCP Tools — 原子命令
# ===========================================================================

@mcp.tool()
def servo_set(angle: float) -> str:
    """Set servo motor angle.

    Args:
        angle: Target angle in degrees, 0 to 180.
               0=full left, 90=center, 180=full right.
    """
    bridge = _ensure_bridge()
    angle = max(0.0, min(180.0, angle))
    msg = Float32()
    msg.data = angle
    bridge.pub_servo.publish(msg)
    return f"[OK] Servo → {angle:.1f}°"


@mcp.tool()
def servo_scan(mode: str = "fast") -> str:
    """Scan servo 0→180→0 (runs entirely on ESP32, smooth motion).

    Args:
        mode: 'fast' (10° steps, ~0.7s) or 'slow' (2° steps, ~3.6s).
    """
    bridge = _ensure_bridge()
    msg = Float32()
    if mode == "slow":
        msg.data = -1.0
    else:
        msg.data = -2.0
    bridge.pub_servo.publish(msg)
    return f"[OK] Servo scan {mode} triggered (executing on ESP32)"


@mcp.tool()
def buzzer_beep(count: int = 1) -> str:
    """Play short beep sounds.

    Args:
        count: Number of beeps, 1 to 10.
    """
    bridge = _ensure_bridge()
    msg = Int32()
    msg.data = max(1, min(10, count))
    bridge.pub_buzzer.publish(msg)
    return f"[OK] Buzzer beep × {msg.data}"


@mcp.tool()
def buzzer_tone(freq: int, duration_ms: int = 500) -> str:
    """Play a tone at a specific frequency.

    The ESP32 plays the tone for ~500ms then stops.

    Args:
        freq: Frequency in Hz (21-20000). Common: C4=262, A4=440, C5=523.
    """
    bridge = _ensure_bridge()
    msg = Int32()
    msg.data = max(21, min(20000, freq))
    bridge.pub_buzzer.publish(msg)
    return f"[OK] Buzzer tone {msg.data} Hz"


@mcp.tool()
def buzzer_melody(name: str) -> str:
    """Play a built-in short sound effect on ESP32.

    For songs/melodies, use play_melody() instead — it sends note data
    over the network and ESP32 plays locally with precise timing.

    Args:
        name: One of:
            - 'startup': ascending arpeggio (short)
            - 'success': happy 3-note (short)
            - 'error': warning beeps (short)
    """
    bridge = _ensure_bridge()
    mapping = {
        "startup": -1, "success": -2, "error": -3,
    }
    val = mapping.get(name.lower().strip())
    if val is None:
        opts = ', '.join(mapping.keys())
        return f"[ERROR] Unknown melody '{name}'. Use: {opts}"
    msg = Int32()
    msg.data = val
    bridge.pub_buzzer.publish(msg)
    return f"[OK] Buzzer melody: {name}"


@mcp.tool()
def buzzer_stop() -> str:
    """Stop the buzzer immediately."""
    bridge = _ensure_bridge()
    msg = Int32()
    msg.data = 0
    bridge.pub_buzzer.publish(msg)
    return "[OK] Buzzer stopped"


@mcp.tool()
def play_melody(melody_data: str) -> str:
    """Play a custom melody on the buzzer (runs entirely on ESP32).

    Send all note data in one message. ESP32 parses and plays locally
    with precise timing — no WiFi jitter issues.

    Format: "freq,duration_ms;freq,duration_ms;..."
    Optional pause prefix: "P30|freq,dur;freq,dur;..."
    (P30 = 30ms pause between notes, default if omitted)

    freq=0 means rest (silence for that duration).

    Common note frequencies (Hz):
    C4=262 D4=294 E4=330 F4=349 G4=392 A4=440 B4=494
    C5=523 D5=587 E5=659 F5=698 G5=784 A5=880 C6=1047

    Example - Twinkle Twinkle first line:
    "262,300;262,300;392,300;392,300;440,300;440,300;392,500"

    Args:
        melody_data: Melody string in "freq,dur;freq,dur;..." format.
    """
    bridge = _ensure_bridge()
    msg = String()
    msg.data = melody_data
    bridge.pub_melody.publish(msg)
    # Count notes
    data = melody_data.split('|')[-1] if '|' in melody_data else melody_data
    note_count = len([n for n in data.split(';') if n.strip()])
    return f"[OK] Playing custom melody ({note_count} notes) on ESP32"


@mcp.tool()
def display_number(value: int) -> str:
    """Show a number on the 4-digit 7-segment display (TM1637).

    Args:
        value: Number 0-9999 to display. Use -1 to clear.
    """
    bridge = _ensure_bridge()
    msg = Int32()
    msg.data = max(-1, min(9999, value))
    bridge.pub_display.publish(msg)
    if value < 0:
        return "[OK] Display cleared"
    return f"[OK] Display → {value}"


@mcp.tool()
def lcd_print(text: str) -> str:
    """Show text on the Grove LCD RGB 16x2 display.

    Args:
        text: Text to display. Up to 16 chars for one line.
              Over 16 chars auto-wraps: first 16 on row 0, rest on row 1.
    """
    bridge = _ensure_bridge()
    msg = String()
    msg.data = text
    bridge.pub_lcd.publish(msg)
    return f"[OK] LCD → \"{text}\""


# ===========================================================================
# MCP Tools — 语义组合
# ===========================================================================

@mcp.tool()
def get_status() -> str:
    """Get ESP32 status: heartbeat (uptime seconds) and current servo angle.

    Call this to verify ESP32 is alive and responding.
    Heartbeat updates every 5 seconds.
    """
    bridge = _ensure_bridge()
    hb = bridge.heartbeat
    sv = bridge.servo_angle
    parts = []
    if hb is not None:
        parts.append(f"uptime={hb}s")
    else:
        parts.append("heartbeat=not received (ESP32 may be offline)")
    if sv is not None:
        parts.append(f"servo={sv:.1f}°")
    else:
        parts.append("servo=unknown")
    return f"[OK] {', '.join(parts)}"


@mcp.tool()
def express(emotion: str) -> str:
    """Express an emotion using all hardware simultaneously.

    Coordinates servo, buzzer, display, and LCD for expressive feedback.

    Args:
        emotion: One of:
            - 'happy': green mood, smile face, success melody, servo nod
            - 'sad': blue mood, crying face, low tone, servo droop
            - 'thinking': scan around, curious face
            - 'yes': green, correct, double beep
            - 'no': red-ish, wrong, error melody
            - 'alert': warning, fast scan, rapid beeps
            - 'celebrate': party mode, fast scan, startup melody
    """
    bridge = _ensure_bridge()
    emotion = emotion.lower().strip()

    configs = {
        "happy": {
            "servo": 0.0, "buzzer": -2, "display": 8888,
            "lcd": "(* v *)  Happy!",
            "desc": "happy (smile, success melody, nod)",
        },
        "sad": {
            "servo": 180.0, "buzzer": -3, "display": 0,
            "lcd": "(T _ T)  Sad...",
            "desc": "sad (crying, error tone, droop)",
        },
        "thinking": {
            "servo": -1.0, "buzzer": 1, "display": -1,
            "lcd": "(o . o)? Hmm...",
            "desc": "thinking (slow scan, curious face)",
        },
        "yes": {
            "servo": 0.0, "buzzer": 2, "display": 1,
            "lcd": "Correct! (^_^)b",
            "desc": "yes/correct (nod, double beep)",
        },
        "no": {
            "servo": 180.0, "buzzer": -3, "display": 0,
            "lcd": "Wrong!   (x_x)",
            "desc": "no/wrong (droop, error sound)",
        },
        "alert": {
            "servo": -2.0, "buzzer": 5, "display": 9999,
            "lcd": "!! ALERT !!",
            "desc": "alert (fast scan, rapid beeps)",
        },
        "celebrate": {
            "servo": -2.0, "buzzer": -1, "display": 8888,
            "lcd": "\\(^o^)/ Party!",
            "desc": "celebrate (fast scan, startup melody)",
        },
    }

    cfg = configs.get(emotion)
    if cfg is None:
        opts = ", ".join(configs.keys())
        return f"[ERROR] Unknown emotion '{emotion}'. Options: {opts}"

    servo_msg = Float32()
    servo_msg.data = cfg["servo"]
    bridge.pub_servo.publish(servo_msg)

    buzzer_msg = Int32()
    buzzer_msg.data = cfg["buzzer"]
    bridge.pub_buzzer.publish(buzzer_msg)

    display_msg = Int32()
    display_msg.data = cfg["display"]
    bridge.pub_display.publish(display_msg)

    lcd_msg = String()
    lcd_msg.data = cfg["lcd"]
    bridge.pub_lcd.publish(lcd_msg)

    return f"[OK] Expressing: {cfg['desc']}"


@mcp.tool()
def reset_hardware() -> str:
    """Reset all hardware to idle state.

    Servo → 90° (center), display → off, LCD → "ESP32 Ready",
    buzzer → stop.
    """
    bridge = _ensure_bridge()

    servo_msg = Float32()
    servo_msg.data = 90.0
    bridge.pub_servo.publish(servo_msg)

    buzzer_msg = Int32()
    buzzer_msg.data = 0
    bridge.pub_buzzer.publish(buzzer_msg)

    display_msg = Int32()
    display_msg.data = -1
    bridge.pub_display.publish(display_msg)

    lcd_msg = String()
    lcd_msg.data = "ESP32 Ready"
    bridge.pub_lcd.publish(lcd_msg)

    return "[OK] All hardware reset to idle state"


# ===========================================================================
# Entry point
# ===========================================================================
if __name__ == "__main__":
    mcp.run()
