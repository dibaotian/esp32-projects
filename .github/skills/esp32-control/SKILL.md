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

## Expression Patterns

The ESP32 can express emotions and perform demos using all peripherals together. Use these patterns when asked to show feelings, play games, or run demos.

### Express: Happy (15s show)
```python
# Phase 1: Wake up greeting
cmd({"cmd":"lcd","act":"rgb","r":0,"g":255,"b":0})
cmd({"cmd":"lcd","act":"print","row":0,"text":"  I am HAPPY!"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"   \\(^o^)/"})
cmd({"cmd":"display","act":"text","value":"-HI-"})
cmd({"cmd":"buzzer","act":"melody","name":"startup"})
# Quick tail-wag shake
for _ in range(4):
    cmd({"cmd":"servo","act":"set","angle":60}); time.sleep(0.15)
    cmd({"cmd":"servo","act":"set","angle":120}); time.sleep(0.15)
cmd({"cmd":"servo","act":"set","angle":90})

# Phase 2: Rainbow colors + ascending notes
colors = [(255,0,0),(255,128,0),(255,255,0),(0,255,0),(0,255,255),(0,0,255),(128,0,255)]
notes = [523, 587, 659, 698, 784, 880, 988]
for i,(r,g,b) in enumerate(colors):
    cmd({"cmd":"lcd","act":"rgb","r":r,"g":g,"b":b})
    cmd({"cmd":"servo","act":"set","angle": 45 + i*20})
    cmd({"cmd":"buzzer","act":"tone","freq":notes[i],"ms":200})
    time.sleep(0.35)

# Phase 3: Party mode - fast color flash + shake
party = [(255,0,0),(0,255,0),(0,0,255),(255,255,0),(255,0,255),(0,255,255)]
for i in range(12):
    r,g,b = party[i % len(party)]
    cmd({"cmd":"lcd","act":"rgb","r":r,"g":g,"b":b})
    cmd({"cmd":"servo","act":"set","angle": 60 if i%2==0 else 120})
    cmd({"cmd":"buzzer","act":"tone","freq":800+(i%3)*200,"ms":80})
    time.sleep(0.2)

# Finale
cmd({"cmd":"servo","act":"set","angle":90})
cmd({"cmd":"lcd","act":"rgb","r":0,"g":255,"b":128})
cmd({"cmd":"lcd","act":"print","row":0,"text":"  Thank You!"})
cmd({"cmd":"display","act":"text","value":"LOVE"})
cmd({"cmd":"buzzer","act":"melody","name":"success"})
```
Full script: `~/Documents/esp32/happy_show.py`

### Express: Sad (15s show)
```python
# Phase 1: Droop head slowly
cmd({"cmd":"lcd","act":"rgb","r":0,"g":0,"b":40})
cmd({"cmd":"lcd","act":"print","row":0,"text":"  I feel sad..."})
cmd({"cmd":"lcd","act":"print","row":1,"text":"    (T_T)"})
for angle in range(90, 19, -2):
    cmd({"cmd":"servo","act":"set","angle":angle}); time.sleep(0.05)
# Descending sigh tones
for freq, ms in [(600,300),(400,400),(250,600)]:
    cmd({"cmd":"buzzer","act":"tone","freq":freq,"ms":ms}); time.sleep(ms/1000+0.2)

# Phase 2: Struggle to lift head, give up 3 times
for attempt in range(3):
    for a in range(20, 50, 3):
        cmd({"cmd":"servo","act":"set","angle":a}); time.sleep(0.03)
    for a in range(50, max(15-attempt*5, 0), -2):
        cmd({"cmd":"servo","act":"set","angle":a}); time.sleep(0.03)
    cmd({"cmd":"buzzer","act":"tone","freq":300-attempt*50,"ms":500}); time.sleep(0.6)

# Phase 3: Fading heartbeat
cmd({"cmd":"lcd","act":"rgb","r":50,"g":0,"b":0})
cmd({"cmd":"servo","act":"set","angle":0})
for freq, ms, gap in [(400,120,0.5),(380,100,0.7),(350,80,0.9),(300,60,1.2)]:
    cmd({"cmd":"buzzer","act":"tone","freq":freq,"ms":ms})
    cmd({"cmd":"lcd","act":"rgb","r":80,"g":0,"b":0}); time.sleep(0.1)
    cmd({"cmd":"lcd","act":"rgb","r":20,"g":0,"b":0}); time.sleep(gap)

# Tiny hope at the end
cmd({"cmd":"lcd","act":"print","row":0,"text":" tomorrow... ?"})
for a in range(0, 45, 2):
    cmd({"cmd":"servo","act":"set","angle":a}); time.sleep(0.04)
cmd({"cmd":"buzzer","act":"tone","freq":523,"ms":800})
```
Full script: `~/Documents/esp32/sad_show.py`

### Express: Yes / Correct (servo nod)
```python
cmd({"cmd":"lcd","act":"rgb","r":0,"g":255,"b":0})
cmd({"cmd":"lcd","act":"print","row":0,"text":"  Correct!"})
cmd({"cmd":"buzzer","act":"melody","name":"success"})
cmd({"cmd":"servo","act":"smooth","angle":0,"speed":60})
time.sleep(5)
cmd({"cmd":"servo","act":"smooth","angle":90,"speed":60})
```

### Express: No / Wrong (servo turn away)
```python
cmd({"cmd":"lcd","act":"rgb","r":255,"g":0,"b":0})
cmd({"cmd":"lcd","act":"print","row":0,"text":"    Wrong!"})
cmd({"cmd":"buzzer","act":"melody","name":"error"})
cmd({"cmd":"servo","act":"smooth","angle":180,"speed":60})
time.sleep(5)
cmd({"cmd":"servo","act":"smooth","angle":90,"speed":60})
```

### Express: Don't Know (servo shake)
```python
cmd({"cmd":"lcd","act":"rgb","r":255,"g":255,"b":0})
cmd({"cmd":"lcd","act":"print","row":0,"text":" I dont know..."})
for _ in range(3):
    cmd({"cmd":"servo","act":"set","angle":60}); time.sleep(0.3)
    cmd({"cmd":"servo","act":"set","angle":120}); time.sleep(0.3)
cmd({"cmd":"servo","act":"smooth","angle":90,"speed":60})
```

### Demo: Pomodoro Timer (25min work + 5min break)
```python
# Work phase - red backlight, countdown on display
cmd({"cmd":"lcd","act":"rgb","r":255,"g":0,"b":0})
cmd({"cmd":"lcd","act":"print","row":0,"text":"  FOCUS TIME"})
cmd({"cmd":"servo","act":"set","angle":0})  # flag down = working
for remaining in range(25*60, 0, -1):
    mins, secs = divmod(remaining, 60)
    cmd({"cmd":"display","act":"number","value": mins*100 + secs})
    cmd({"cmd":"display","act":"colon","on": remaining % 2 == 0})
    cmd({"cmd":"lcd","act":"print","row":1,"text": f"  {mins:02d}:{secs:02d} left"})
    time.sleep(1)
# Time's up!
cmd({"cmd":"servo","act":"set","angle":180})  # flag up = break
cmd({"cmd":"lcd","act":"rgb","r":0,"g":255,"b":0})
cmd({"cmd":"lcd","act":"print","row":0,"text":" BREAK TIME!"})
cmd({"cmd":"buzzer","act":"melody","name":"success"})
```

### Demo: Morse Code
```python
MORSE = {'A':'.-','B':'-...','C':'-.-.','D':'-..','E':'.','F':'..-.','G':'--.','H':'....',
'I':'..','J':'.---','K':'-.-','L':'.-..','M':'--','N':'-.','O':'---','P':'.--.','Q':'--.-',
'R':'.-.','S':'...','T':'-','U':'..-','V':'...-','W':'.--','X':'-..-','Y':'-.--','Z':'--..'}
def play_morse(text):
    for ch in text.upper():
        if ch == ' ':
            time.sleep(0.7)
            continue
        code = MORSE.get(ch, '')
        cmd({"cmd":"lcd","act":"print","row":1,"text": f" {ch} = {code}"})
        for symbol in code:
            ms = 200 if symbol == '.' else 600
            cmd({"cmd":"buzzer","act":"tone","freq":800,"ms":ms})
            cmd({"cmd":"servo","act":"set","angle": 0 if symbol=='.' else 180})
            time.sleep(ms/1000 + 0.1)
        cmd({"cmd":"servo","act":"set","angle":90})
        time.sleep(0.3)
play_morse("SOS")
```

### Demo: Music Player (Super Mario theme)
```python
mario = [(660,100),(660,100),(0,100),(660,100),(0,100),(510,100),(660,100),
         (0,100),(770,100),(0,300),(380,100),(0,300)]
cmd({"cmd":"lcd","act":"print","row":0,"text":" Super Mario!"})
cmd({"cmd":"lcd","act":"rgb","r":255,"g":0,"b":0})
for freq, ms in mario:
    if freq > 0:
        cmd({"cmd":"buzzer","act":"tone","freq":freq,"ms":ms})
        cmd({"cmd":"servo","act":"set","angle": int(freq/5) % 180})
    time.sleep(ms/1000 + 0.05)
cmd({"cmd":"servo","act":"set","angle":90})
```

### Demo: Q&A Game (True/False with servo feedback)
```python
# Servo at 90° = ready, 0° = correct, 180° = wrong, shake = don't know
cmd({"cmd":"servo","act":"set","angle":90})
cmd({"cmd":"lcd","act":"print","row":0,"text":"  Ask me!"})

def answer_yes():
    cmd({"cmd":"lcd","act":"rgb","r":0,"g":255,"b":0})
    cmd({"cmd":"buzzer","act":"melody","name":"success"})
    cmd({"cmd":"servo","act":"smooth","angle":0,"speed":60})
    time.sleep(5)
    cmd({"cmd":"servo","act":"smooth","angle":90,"speed":60})

def answer_no():
    cmd({"cmd":"lcd","act":"rgb","r":255,"g":0,"b":0})
    cmd({"cmd":"buzzer","act":"melody","name":"error"})
    cmd({"cmd":"servo","act":"smooth","angle":180,"speed":60})
    time.sleep(5)
    cmd({"cmd":"servo","act":"smooth","angle":90,"speed":60})

def answer_unknown():
    cmd({"cmd":"lcd","act":"rgb","r":255,"g":255,"b":0})
    for _ in range(3):
        cmd({"cmd":"servo","act":"set","angle":60}); time.sleep(0.3)
        cmd({"cmd":"servo","act":"set","angle":120}); time.sleep(0.3)
    cmd({"cmd":"servo","act":"smooth","angle":90,"speed":60})
```

## Pre-built Demo Scripts

| Script | Description | Run Command |
|--------|-------------|-------------|
| `happy_show.py` | 15s happy expression with all peripherals | `python3 ~/Documents/esp32/happy_show.py` |
| `sad_show.py` | 15s melancholy show with fading heartbeat | `python3 ~/Documents/esp32/sad_show.py` |
| `play_game.sh` | Interactive Q&A game shell script | `bash ~/Documents/esp32/play_game.sh` |
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
