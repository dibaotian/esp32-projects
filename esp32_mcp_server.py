#!/usr/bin/env python3
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

"""
ESP32 MCP Server — Model Context Protocol interface for AI Agents

Exposes ESP32 hardware (servo, buzzer, TM1637, Grove LCD) as MCP tools.
AI agents (OpenClaw, Copilot, etc.) can control physical hardware via
structured tool calls instead of raw TCP/JSON.

Architecture:
  AI Agent → MCP Protocol (stdio) → This Server → TCP/JSON → ESP32

Usage:
  # Direct run (for testing)
  python3 esp32_mcp_server.py

  # VS Code / OpenClaw config
  "mcpServers": {
    "esp32": {
      "command": "python3",
      "args": ["/path/to/esp32_mcp_server.py"]
    }
  }
"""

import json
import socket
import threading
import time
from contextlib import contextmanager
from typing import Any

from mcp.server.fastmcp import FastMCP

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
ESP32_IP = "192.168.4.1"
ESP32_PORT = 3333
CONNECT_TIMEOUT = 5
CMD_TIMEOUT = 3
MAX_CMD_PER_SEC = 10

# ---------------------------------------------------------------------------
# MCP Server
# ---------------------------------------------------------------------------
mcp = FastMCP(
    "esp32",
    instructions=(
        "ESP32 hardware controller. Controls servo motor, buzzer, "
        "TM1637 7-segment display, and Grove LCD RGB via WiFi TCP. "
        "The host must be connected to WiFi SSID 'ESP32-Control' "
        "(password: esp32ctrl) for commands to work."
    ),
)

# ---------------------------------------------------------------------------
# TCP Connection Manager (thread-safe, lazy, auto-reconnect)
# ---------------------------------------------------------------------------
class ESP32Connection:
    """Manages a persistent TCP connection to the ESP32."""

    def __init__(self, ip: str = ESP32_IP, port: int = ESP32_PORT):
        self._ip = ip
        self._port = port
        self._sock: socket.socket | None = None
        self._lock = threading.Lock()
        self._last_cmd_time = 0.0
        self._min_interval = 1.0 / MAX_CMD_PER_SEC
        # Track last known state
        self._state: dict[str, Any] = {
            "servo_angle": None,
            "lcd_row0": None,
            "lcd_row1": None,
            "lcd_rgb": None,
            "display_value": None,
            "connected": False,
        }

    @property
    def state(self) -> dict[str, Any]:
        return dict(self._state)

    def _connect(self) -> socket.socket:
        """Create a new TCP connection."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(CONNECT_TIMEOUT)
        sock.connect((self._ip, self._port))
        sock.settimeout(CMD_TIMEOUT)
        self._state["connected"] = True
        return sock

    def _ensure_connected(self) -> socket.socket:
        """Return existing connection or create a new one."""
        if self._sock is None:
            self._sock = self._connect()
        return self._sock

    def _disconnect(self):
        """Close the connection."""
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
            self._state["connected"] = False

    def send_command(self, cmd_dict: dict) -> dict:
        """Send a JSON command and return the parsed response."""
        with self._lock:
            # Rate limit
            now = time.monotonic()
            wait = self._min_interval - (now - self._last_cmd_time)
            if wait > 0:
                time.sleep(wait)
            self._last_cmd_time = time.monotonic()

            # Try send, reconnect once on failure
            for attempt in range(2):
                try:
                    sock = self._ensure_connected()
                    payload = json.dumps(cmd_dict) + "\n"
                    sock.sendall(payload.encode("utf-8"))
                    data = sock.recv(4096)
                    if not data:
                        raise ConnectionError("Connection closed by ESP32")
                    text = data.decode("utf-8", errors="replace").strip()
                    try:
                        return json.loads(text)
                    except json.JSONDecodeError:
                        return {"status": "ok", "raw": text}
                except (OSError, ConnectionError):
                    self._disconnect()
                    if attempt == 0:
                        continue
                    return {"status": "error", "msg": f"Cannot connect to ESP32 at {self._ip}:{self._port}"}

    def close(self):
        with self._lock:
            self._disconnect()


# Singleton connection
_conn = ESP32Connection()


def _cmd(d: dict) -> dict:
    """Send command and update state cache."""
    result = _conn.send_command(d)
    # Update state cache on success
    if result.get("status") == "ok":
        cmd = d.get("cmd")
        act = d.get("act")
        if cmd == "servo" and act == "set":
            _conn._state["servo_angle"] = d.get("angle")
        elif cmd == "servo" and act == "get":
            _conn._state["servo_angle"] = result.get("angle")
        elif cmd == "lcd" and act == "print":
            row = d.get("row", 0)
            _conn._state[f"lcd_row{row}"] = d.get("text")
        elif cmd == "lcd" and act == "rgb":
            _conn._state["lcd_rgb"] = (d.get("r"), d.get("g"), d.get("b"))
        elif cmd == "lcd" and act == "clear":
            _conn._state["lcd_row0"] = None
            _conn._state["lcd_row1"] = None
        elif cmd == "display" and act == "number":
            _conn._state["display_value"] = d.get("value")
        elif cmd == "display" and act == "text":
            _conn._state["display_value"] = d.get("value")
        elif cmd == "display" and act == "clear":
            _conn._state["display_value"] = None
    return result


def _format_result(result: dict) -> str:
    """Format ESP32 response as readable string."""
    status = result.get("status", "unknown")
    msg = result.get("msg", "")
    icon = "OK" if status == "ok" else "ERROR"
    extra = {k: v for k, v in result.items() if k not in ("status", "cmd", "act", "msg")}
    line = f"[{icon}] {msg}"
    if extra:
        line += f" | {extra}"
    return line


# ===========================================================================
# Layer 1: Atomic Tools — Direct hardware control
# ===========================================================================

@mcp.tool()
def servo_set(angle: float) -> str:
    """Set servo motor angle.

    Args:
        angle: Target angle in degrees, 0 to 180.
               0°=left, 90°=center, 180°=right.
    """
    angle = max(0.0, min(180.0, angle))
    return _format_result(_cmd({"cmd": "servo", "act": "set", "angle": angle}))


@mcp.tool()
def servo_smooth(angle: float, speed: float = 60.0) -> str:
    """Move servo smoothly to target angle at given speed.

    Args:
        angle: Target angle 0-180.
        speed: Movement speed in degrees/second (e.g. 30=slow, 60=medium, 120=fast).
    """
    angle = max(0.0, min(180.0, angle))
    speed = max(1.0, min(360.0, speed))
    return _format_result(_cmd({"cmd": "servo", "act": "smooth", "angle": angle, "speed": speed}))


@mcp.tool()
def servo_get() -> str:
    """Get the current servo angle. Returns the angle in degrees."""
    return _format_result(_cmd({"cmd": "servo", "act": "get"}))


@mcp.tool()
def servo_sweep(start: float = 0, end: float = 180, speed: float = 45, count: int = 1) -> str:
    """Sweep servo back and forth between two angles.

    Args:
        start: Start angle 0-180.
        end: End angle 0-180.
        speed: Degrees per second.
        count: Number of round trips. 0 = one-way only.
    """
    return _format_result(_cmd({
        "cmd": "servo", "act": "sweep",
        "start": start, "end": end, "speed": speed, "count": count
    }))


@mcp.tool()
def buzzer_beep(count: int = 1) -> str:
    """Play short beep sounds.

    Args:
        count: Number of beeps, 1 to 20.
    """
    count = max(1, min(20, count))
    return _format_result(_cmd({"cmd": "buzzer", "act": "beep", "count": count}))


@mcp.tool()
def buzzer_tone(freq: int, ms: int) -> str:
    """Play a tone at a specific frequency for a duration.

    Args:
        freq: Frequency in Hz (20-20000). Common: C4=262, C5=523, A4=440.
        ms: Duration in milliseconds (1-10000).
    """
    freq = max(20, min(20000, freq))
    ms = max(1, min(10000, ms))
    return _format_result(_cmd({"cmd": "buzzer", "act": "tone", "freq": freq, "ms": ms}))


@mcp.tool()
def buzzer_melody(name: str) -> str:
    """Play a built-in melody.

    Args:
        name: Melody name. Options: 'startup' (ascending arpeggio),
              'success' (happy 3-note), 'error' (warning beeps).
    """
    return _format_result(_cmd({"cmd": "buzzer", "act": "melody", "name": name}))


@mcp.tool()
def display_number(value: int) -> str:
    """Show a number on the 4-digit 7-segment display (TM1637).

    Args:
        value: Number 0-9999 to display.
    """
    value = max(0, min(9999, value))
    return _format_result(_cmd({"cmd": "display", "act": "number", "value": value}))


@mcp.tool()
def display_text(value: str) -> str:
    """Show text on the 4-digit 7-segment display.

    Args:
        value: Up to 4 characters. Supports: 0-9, A-F, dash, space.
    """
    return _format_result(_cmd({"cmd": "display", "act": "text", "value": value[:4]}))


@mcp.tool()
def display_clear() -> str:
    """Clear the 7-segment display."""
    return _format_result(_cmd({"cmd": "display", "act": "clear"}))


@mcp.tool()
def lcd_print(text: str, row: int = 0) -> str:
    """Print text on the Grove LCD (16x2 characters).

    Args:
        text: Text to display, max 16 characters.
        row: Row number — 0 (top) or 1 (bottom).
    """
    row = 0 if row not in (0, 1) else row
    return _format_result(_cmd({"cmd": "lcd", "act": "print", "row": row, "text": text[:16]}))


@mcp.tool()
def lcd_rgb(r: int, g: int, b: int) -> str:
    """Set LCD backlight color.

    Args:
        r: Red 0-255.
        g: Green 0-255.
        b: Blue 0-255.
    Common: red=(255,0,0), green=(0,255,0), blue=(0,0,255),
            white=(255,255,255), yellow=(255,255,0), purple=(255,0,255).
    """
    r, g, b = max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b))
    return _format_result(_cmd({"cmd": "lcd", "act": "rgb", "r": r, "g": g, "b": b}))


@mcp.tool()
def lcd_clear() -> str:
    """Clear all text on the LCD screen."""
    return _format_result(_cmd({"cmd": "lcd", "act": "clear"}))


# ===========================================================================
# Layer 2: Semantic / Composite Tools — High-level actions
# ===========================================================================

@mcp.tool()
def express(emotion: str) -> str:
    """Express an emotion using all hardware simultaneously.

    This triggers a coordinated multi-device animation.

    Args:
        emotion: One of 'happy', 'sad', 'yes', 'no', 'thinking', 'alert', 'celebrate'.
    """
    emotion = emotion.lower().strip()

    if emotion == "happy":
        _cmd({"cmd": "lcd", "act": "rgb", "r": 0, "g": 255, "b": 0})
        _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "  (* v *)  "})
        _cmd({"cmd": "lcd", "act": "print", "row": 1, "text": "   Happy!   "})
        _cmd({"cmd": "display", "act": "text", "value": "GOOD"})
        _cmd({"cmd": "servo", "act": "set", "angle": 0})
        _cmd({"cmd": "buzzer", "act": "melody", "name": "success"})
        return "Expressing: happy (green light, smile face, success melody, servo nod)"

    elif emotion == "sad":
        _cmd({"cmd": "lcd", "act": "rgb", "r": 0, "g": 0, "b": 255})
        _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "  (T _ T)  "})
        _cmd({"cmd": "lcd", "act": "print", "row": 1, "text": "   Sad...   "})
        _cmd({"cmd": "display", "act": "text", "value": " -- "})
        _cmd({"cmd": "servo", "act": "smooth", "angle": 180, "speed": 20})
        _cmd({"cmd": "buzzer", "act": "tone", "freq": 200, "ms": 800})
        return "Expressing: sad (blue light, crying face, low tone, servo droop)"

    elif emotion == "yes":
        _cmd({"cmd": "lcd", "act": "rgb", "r": 0, "g": 255, "b": 0})
        _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "  Correct!"})
        _cmd({"cmd": "display", "act": "text", "value": " YES"})
        _cmd({"cmd": "servo", "act": "set", "angle": 0})
        _cmd({"cmd": "buzzer", "act": "beep", "count": 2})
        return "Expressing: yes/correct (green, servo to 0°, double beep)"

    elif emotion == "no":
        _cmd({"cmd": "lcd", "act": "rgb", "r": 255, "g": 0, "b": 0})
        _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "  Wrong!"})
        _cmd({"cmd": "display", "act": "text", "value": " NO "})
        _cmd({"cmd": "servo", "act": "set", "angle": 180})
        _cmd({"cmd": "buzzer", "act": "melody", "name": "error"})
        return "Expressing: no/wrong (red, servo to 180°, error melody)"

    elif emotion == "thinking":
        _cmd({"cmd": "lcd", "act": "rgb", "r": 255, "g": 255, "b": 0})
        _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "  (o . o)? "})
        _cmd({"cmd": "lcd", "act": "print", "row": 1, "text": " Thinking..."})
        _cmd({"cmd": "display", "act": "text", "value": "    "})
        _cmd({"cmd": "servo", "act": "sweep", "start": 70, "end": 110, "speed": 30, "count": 2})
        return "Expressing: thinking (yellow, curious face, slow sweep)"

    elif emotion == "alert":
        _cmd({"cmd": "lcd", "act": "rgb", "r": 255, "g": 0, "b": 0})
        _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "  !! ALERT !!"})
        _cmd({"cmd": "lcd", "act": "print", "row": 1, "text": "  ATTENTION  "})
        _cmd({"cmd": "display", "act": "text", "value": "HELP"})
        _cmd({"cmd": "servo", "act": "sweep", "start": 0, "end": 180, "speed": 120, "count": 2})
        _cmd({"cmd": "buzzer", "act": "beep", "count": 5})
        return "Expressing: alert (red flash, fast sweep, rapid beeps)"

    elif emotion == "celebrate":
        _cmd({"cmd": "lcd", "act": "rgb", "r": 255, "g": 0, "b": 255})
        _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": " \\(^o^)/ "})
        _cmd({"cmd": "lcd", "act": "print", "row": 1, "text": " Celebrate!! "})
        _cmd({"cmd": "display", "act": "number", "value": 8888})
        _cmd({"cmd": "servo", "act": "sweep", "start": 0, "end": 180, "speed": 90, "count": 3})
        _cmd({"cmd": "buzzer", "act": "melody", "name": "startup"})
        return "Expressing: celebrate (purple, party face, fast sweep, startup melody)"

    else:
        return f"Unknown emotion: '{emotion}'. Available: happy, sad, yes, no, thinking, alert, celebrate"


@mcp.tool()
def gauge(value: float, label: str = "Value", unit: str = "%") -> str:
    """Display a value as a physical gauge using servo + LCD + display.

    Servo acts as a pointer (0°=0%, 180°=100%).
    LCD shows label and bar graph.
    TM1637 shows the numeric value.
    Backlight changes: green(<50) → yellow(50-80) → red(>80).

    Args:
        value: Value to display, 0 to 100.
        label: Label shown on LCD row 0 (max 16 chars).
        unit: Unit suffix shown after the number (default: '%').
    """
    value = max(0.0, min(100.0, value))
    v_int = int(round(value))
    angle = int(value * 1.8)

    # Color gradient
    if value < 50:
        r, g, b = int(value * 5.1), 255, 0
    elif value < 80:
        r, g, b = 255, max(0, int(255 - (value - 50) * 8.5)), 0
    else:
        r, g, b = 255, 0, 0

    # Bar graph
    filled = int(value / 100 * 12)
    bar = "[" + "=" * filled + " " * (12 - filled) + "]"

    _cmd({"cmd": "servo", "act": "set", "angle": angle})
    _cmd({"cmd": "display", "act": "number", "value": v_int})
    _cmd({"cmd": "lcd", "act": "rgb", "r": r, "g": g, "b": b})
    _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": label[:16]})
    _cmd({"cmd": "lcd", "act": "print", "row": 1, "text": f"{bar}{v_int:3d}{unit}"})

    if value > 90:
        _cmd({"cmd": "buzzer", "act": "beep", "count": 2})

    return f"Gauge: {v_int}{unit} (servo={angle}°, color=({r},{g},{b}), bar={bar})"


@mcp.tool()
def reset_hardware() -> str:
    """Reset all hardware to default idle state.

    Servo → 90° (center), LCD → blue backlight with 'Ready',
    Display → '----', buzzer silent.
    """
    _cmd({"cmd": "servo", "act": "smooth", "angle": 90, "speed": 60})
    _cmd({"cmd": "lcd", "act": "clear"})
    _cmd({"cmd": "lcd", "act": "rgb", "r": 0, "g": 0, "b": 255})
    _cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "  ESP32 Ready"})
    _cmd({"cmd": "lcd", "act": "print", "row": 1, "text": " Awaiting cmd..."})
    _cmd({"cmd": "display", "act": "text", "value": "----"})
    return "Hardware reset to idle state (servo=90°, blue LCD, display='----')"


@mcp.tool()
def get_status() -> str:
    """Get the current cached state of all hardware components.

    Returns last known values for servo angle, LCD text, backlight color,
    display value, and connection status.
    """
    state = _conn.state
    lines = [
        f"Connected: {state['connected']}",
        f"Servo angle: {state['servo_angle']}",
        f"LCD row 0: {state['lcd_row0']}",
        f"LCD row 1: {state['lcd_row1']}",
        f"LCD RGB: {state['lcd_rgb']}",
        f"Display: {state['display_value']}",
    ]
    return "\n".join(lines)


@mcp.tool()
def send_raw_json(json_str: str) -> str:
    """Send a raw JSON command string directly to ESP32.

    Use this for advanced commands not covered by other tools.

    Args:
        json_str: Valid JSON string, e.g. '{"cmd":"sys","act":"help"}'
    """
    try:
        cmd_dict = json.loads(json_str)
    except json.JSONDecodeError as e:
        return f"Invalid JSON: {e}"
    return _format_result(_cmd(cmd_dict))


# ===========================================================================
# Resources — Read-only reference data
# ===========================================================================

@mcp.resource("esp32://hardware")
def hardware_info() -> str:
    """ESP32 hardware configuration and GPIO mapping."""
    return """ESP32 Hardware Configuration
============================
Board: ESP32-DevKitC (PCB antenna)
Chip: ESP32-D0WDQ6, Dual Core 240MHz, 4MB Flash

WiFi AP:
  SSID: ESP32-Control
  Password: esp32ctrl
  IP: 192.168.4.1
  TCP Port: 3333

GPIO Mapping:
  GPIO 25 — Passive Buzzer (LEDC PWM, Timer 0, Channel 0)
  GPIO 27 — Servo Motor 180° (LEDC PWM 50Hz, Timer 1, Channel 1)
  GPIO 16 — TM1637 CLK (7-segment display clock)
  GPIO 17 — TM1637 DIO (7-segment display data, needs 5V VCC)
  GPIO 18 — Grove LCD SDA (I2C, 16x2 + RGB backlight)
  GPIO 23 — Grove LCD SCL (I2C clock)

PCA9632 RGB Note:
  Register PWM0 = Blue, PWM1 = Green, PWM2 = Red (NOT R,G,B order)
"""


@mcp.resource("esp32://protocol")
def protocol_summary() -> str:
    """Summary of the JSON command protocol."""
    return """ESP32 JSON Command Protocol
===========================
Format: {"cmd": "<module>", "act": "<action>", ...params}

Modules:
  buzzer  — beep, tone, melody, volume, stop
  servo   — set, get, smooth, sweep, pulse, stop
  display — number, text, raw, clear, bright, colon
  lcd     — print, clear, rgb, cursor, on, off
  sys     — help

Examples:
  {"cmd":"servo","act":"set","angle":90}
  {"cmd":"buzzer","act":"beep","count":3}
  {"cmd":"display","act":"number","value":1234}
  {"cmd":"lcd","act":"print","row":0,"text":"Hello"}
  {"cmd":"lcd","act":"rgb","r":255,"g":0,"b":0}
"""


@mcp.resource("esp32://emotions")
def emotion_reference() -> str:
    """Available emotions for the express() tool."""
    return """Available Emotions for express()
================================
happy     — Green light, smile face, success melody, servo nod (0°)
sad       — Blue light, crying face, low tone, servo droop (180°)
yes       — Green light, 'Correct!', servo 0°, double beep
no        — Red light, 'Wrong!', servo 180°, error melody
thinking  — Yellow light, curious face, slow servo sweep
alert     — Red light, 'ALERT!', fast sweep, rapid beeps
celebrate — Purple light, party face, fast sweep, startup melody
"""


# ===========================================================================
# Entry point
# ===========================================================================

if __name__ == "__main__":
    mcp.run()
