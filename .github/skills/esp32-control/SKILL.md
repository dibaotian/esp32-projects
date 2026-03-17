---
name: esp32-control
description: 'Control ESP32 hardware via WiFi TCP JSON protocol. Use when: controlling servo motor, buzzer, 7-segment display (TM1637), Grove LCD RGB display, sending commands to ESP32, running hardware self-test, sweeping servo, displaying numbers/text. Keywords: ESP32, servo, buzzer, display, TM1637, LCD, Grove, RGB, GPIO, motor control, IoT.'
argument-hint: 'Describe what you want the ESP32 to do (e.g., "sweep servo 0-180", "display 1234", "beep 3 times")'
---

# ESP32 Hardware Control

Control ESP32 peripherals (servo, buzzer, 7-segment display) via WiFi TCP JSON commands.

## When to Use

- Control servo motor angle (0~180°)
- Sound buzzer (beep, tone, melody)
- Display numbers/text on TM1637 4-digit 7-segment display
- Display text on Grove LCD RGB 16x2 character display with RGB backlight
- Run hardware self-tests
- Build interactive demos (e.g., Q&A game with physical feedback)

## Prerequisites

1. ESP32 board must be powered on and running the `wifi_echo` firmware
2. Host must connect to ESP32's WiFi AP before sending commands:
   ```bash
   bash ~/Documents/esp32/wifi_echo/setup_dual_network.sh
   ```

## Connection

| Parameter | Value |
|-----------|-------|
| WiFi SSID | `ESP32-Control` |
| WiFi Password | `esp32ctrl` |
| ESP32 IP | `192.168.4.1` |
| TCP Port | `3333` |
| Encoding | UTF-8 |
| Message terminator | `\n` |

## Python Control Template

Use this template to send commands:

```python
import socket, json, time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("192.168.4.1", 3333))
sock.settimeout(3)

def cmd(d):
    """Send a JSON command and consume the response."""
    sock.send((json.dumps(d) + "\n").encode())
    time.sleep(0.15)
    try:
        return sock.recv(4096).decode()
    except:
        return ""

# ... send commands ...

sock.close()
```

## Command Reference

### Servo (cmd: "servo")

| Action | JSON | Description |
|--------|------|-------------|
| Set angle | `{"cmd":"servo","act":"set","angle":90}` | Move to angle (0~180) |
| Get angle | `{"cmd":"servo","act":"get"}` | Read current angle |
| Smooth move | `{"cmd":"servo","act":"smooth","angle":90,"speed":60}` | Smooth move at °/s |
| Sweep | `{"cmd":"servo","act":"sweep","start":0,"end":180,"speed":45,"count":1}` | Oscillate (blocking) |
| Set pulse | `{"cmd":"servo","act":"pulse","us":1500}` | Raw pulse width μs |
| Stop | `{"cmd":"servo","act":"stop"}` | Release torque |

### Buzzer (cmd: "buzzer")

| Action | JSON | Description |
|--------|------|-------------|
| Beep | `{"cmd":"buzzer","act":"beep","count":3}` | Short beeps (1~20) |
| Tone | `{"cmd":"buzzer","act":"tone","freq":1000,"ms":500}` | Frequency + duration |
| Melody | `{"cmd":"buzzer","act":"melody","name":"startup"}` | Play preset melody |
| Volume | `{"cmd":"buzzer","act":"volume","value":30}` | Set volume (0~100) |
| Stop | `{"cmd":"buzzer","act":"stop"}` | Stop sound |

### Display (cmd: "display")

| Action | JSON | Description |
|--------|------|-------------|
| Number | `{"cmd":"display","act":"number","value":1234}` | Show 0~9999 |
| Text | `{"cmd":"display","act":"text","value":"AbCd"}` | Show string (0-9,A-F,-,space) |
| Raw | `{"cmd":"display","act":"raw","segs":[63,6,91,79]}` | Direct segment codes |
| Brightness | `{"cmd":"display","act":"bright","value":7}` | Brightness 0~7 |
| Colon | `{"cmd":"display","act":"colon","on":true}` | Toggle colon |
| Clear | `{"cmd":"display","act":"clear"}` | Clear display |

### LCD (cmd: "lcd")

Grove LCD RGB 16x2 character display with RGB backlight (I2C: SDA=GPIO 18, SCL=GPIO 23).

| Action | JSON | Description |
|--------|------|-------------|
| Print | `{"cmd":"lcd","act":"print","row":0,"text":"Hello"}` | Show text on row 0 or 1 (max 16 chars) |
| Clear | `{"cmd":"lcd","act":"clear"}` | Clear LCD screen |
| RGB | `{"cmd":"lcd","act":"rgb","r":0,"g":0,"b":255}` | Set backlight color |
| Cursor | `{"cmd":"lcd","act":"cursor","col":0,"row":0}` | Set cursor position |
| On | `{"cmd":"lcd","act":"on"}` | Turn on display + backlight |
| Off | `{"cmd":"lcd","act":"off"}` | Turn off display + backlight |

### System (cmd: "sys")

| Action | JSON | Description |
|--------|------|-------------|
| Help | `{"cmd":"sys","act":"help"}` | List modules |

## Response Format

```json
{"status": "ok", "cmd": "servo", "act": "set", "msg": "angle=90.0"}
```

- `status`: `"ok"` or `"error"`
- `msg`: Human-readable description

## Common Patterns

### Servo + Display sync
```python
for angle in range(0, 181):
    cmd({"cmd": "servo", "act": "set", "angle": angle})
    cmd({"cmd": "display", "act": "number", "value": angle})
    time.sleep(0.2)
```

### Self-test sequence
```python
cmd({"cmd": "servo", "act": "set", "angle": 0})    # Min
cmd({"cmd": "servo", "act": "set", "angle": 90})   # Center
cmd({"cmd": "servo", "act": "set", "angle": 180})  # Max
cmd({"cmd": "servo", "act": "sweep", "start": 0, "end": 180, "speed": 90, "count": 1})
cmd({"cmd": "servo", "act": "smooth", "angle": 90, "speed": 60})  # Return
```

### Blocking commands
`smooth`, `sweep`, and `tone` are blocking — they complete before responding. Allow enough `time.sleep()` or use a longer `sock.settimeout()`.

## Hardware Specs

| Component | GPIO | Details |
|-----------|------|---------|
| Servo | 27 | 180° servo, 50Hz PWM, 500~2500μs |
| Buzzer | 25 | Passive buzzer, LEDC PWM |
| TM1637 CLK | 16 | 7-segment display clock |
| TM1637 DIO | 17 | 7-segment display data |
| LCD SDA | 18 | Grove LCD I2C data |
| LCD SCL | 23 | Grove LCD I2C clock |

## Full Protocol Documentation

For complete protocol details, see [PROTOCOL.md](./references/PROTOCOL.md).
