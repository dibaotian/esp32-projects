#!/usr/bin/env python3
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

"""
ESP32 CPU Monitor Dashboard

Servo = pointer gauge (0°=0%, 180°=100%)
TM1637 = CPU percentage number
LCD row 0 = "CPU Monitor"
LCD row 1 = percentage bar + value
Backlight = green(<50%) / yellow(50-80%) / red(>80%)
Buzzer = alert beep when >90%

Usage: python3 cpu_monitor.py [duration_seconds]
Default: runs for 60 seconds. Use 0 for infinite.
"""

import socket, json, time, sys

ESP32_IP = "192.168.4.1"
ESP32_PORT = 3333
INTERVAL = 1.0  # update interval in seconds

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((ESP32_IP, ESP32_PORT))
sock.settimeout(2)

def cmd(d):
    sock.send((json.dumps(d) + "\n").encode())
    time.sleep(0.05)
    try: sock.recv(4096)
    except: pass

def get_cpu_times():
    """Read /proc/stat and return (idle, total) for first CPU line."""
    with open("/proc/stat") as f:
        line = f.readline()  # first line = aggregate cpu
    parts = line.split()
    # user, nice, system, idle, iowait, irq, softirq, steal
    times = [int(x) for x in parts[1:]]
    idle = times[3] + times[4]  # idle + iowait
    total = sum(times)
    return idle, total

def make_bar(pct, width=14):
    """Create a text progress bar like [========    ]"""
    filled = int(pct / 100 * width)
    return "[" + "=" * filled + " " * (width - filled) + "]"

def get_color(pct):
    """Return RGB based on CPU percentage."""
    if pct < 50:
        # Green to yellow gradient
        r = int(pct * 5.1)
        return (r, 255, 0)
    elif pct < 80:
        # Yellow to red gradient
        g = int(255 - (pct - 50) * 8.5)
        return (255, max(g, 0), 0)
    else:
        # Red, pulsing intensity
        return (255, 0, 0)

# --- Init display ---
cmd({"cmd": "lcd", "act": "clear"})
cmd({"cmd": "lcd", "act": "print", "row": 0, "text": " CPU Monitor"})
cmd({"cmd": "lcd", "act": "rgb", "r": 0, "g": 255, "b": 0})
cmd({"cmd": "display", "act": "text", "value": "CPU "})
cmd({"cmd": "servo", "act": "set", "angle": 0})
cmd({"cmd": "buzzer", "act": "beep", "count": 1})

duration = int(sys.argv[1]) if len(sys.argv) > 1 else 60
print(f"CPU Monitor running for {'∞' if duration == 0 else str(duration) + 's'}. Ctrl+C to stop.")

prev_idle, prev_total = get_cpu_times()
time.sleep(0.1)

alert_cooldown = 0
elapsed = 0

try:
    while duration == 0 or elapsed < duration:
        # Calculate CPU usage
        idle, total = get_cpu_times()
        d_idle = idle - prev_idle
        d_total = total - prev_total
        cpu_pct = (1.0 - d_idle / d_total) * 100 if d_total > 0 else 0
        cpu_pct = max(0, min(100, cpu_pct))
        prev_idle, prev_total = idle, total

        cpu_int = int(round(cpu_pct))

        # Servo angle: 0% = 0°, 100% = 180°
        angle = int(cpu_pct * 1.8)
        angle = max(0, min(180, angle))

        # Colors
        r, g, b = get_color(cpu_pct)

        # Progress bar for LCD row 1
        bar = make_bar(cpu_pct, 12)
        row1 = f"{bar}{cpu_int:3d}%"

        # Send commands
        cmd({"cmd": "servo", "act": "set", "angle": angle})
        cmd({"cmd": "display", "act": "number", "value": cpu_int})
        cmd({"cmd": "lcd", "act": "rgb", "r": r, "g": g, "b": b})
        cmd({"cmd": "lcd", "act": "print", "row": 1, "text": row1})

        # Alert beep if >90%
        if cpu_pct > 90 and alert_cooldown <= 0:
            cmd({"cmd": "buzzer", "act": "beep", "count": 2})
            alert_cooldown = 5  # don't beep again for 5 seconds

        # Terminal output
        bar_term = "█" * int(cpu_pct / 2) + "░" * (50 - int(cpu_pct / 2))
        color_tag = "🟢" if cpu_pct < 50 else ("🟡" if cpu_pct < 80 else "🔴")
        print(f"\r{color_tag} CPU: {cpu_int:3d}% |{bar_term}| servo={angle:3d}°", end="", flush=True)

        alert_cooldown -= INTERVAL
        elapsed += INTERVAL
        time.sleep(INTERVAL)

except KeyboardInterrupt:
    print("\n\nStopping...")

# --- Cleanup: return to idle state ---
cmd({"cmd": "servo", "act": "smooth", "angle": 90, "speed": 60})
cmd({"cmd": "lcd", "act": "rgb", "r": 0, "g": 0, "b": 255})
cmd({"cmd": "lcd", "act": "print", "row": 0, "text": "Hi Min I'm ready"})
cmd({"cmd": "lcd", "act": "print", "row": 1, "text": "Feed me token!"})
cmd({"cmd": "display", "act": "text", "value": "8888"})

sock.close()
print("Monitor stopped.")
