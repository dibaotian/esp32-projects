#!/usr/bin/env python3
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

"""ESP32 Happy Expression - 15 second show using all peripherals"""

import socket, json, time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("192.168.4.1", 3333))
sock.settimeout(2)

def cmd(d):
    sock.send((json.dumps(d) + "\n").encode())
    time.sleep(0.08)
    try: sock.recv(4096)
    except: pass

# ===== Phase 1 (0-3s): Wake up! I'm happy! =====
cmd({"cmd":"lcd","act":"clear"})
cmd({"cmd":"lcd","act":"rgb","r":0,"g":255,"b":0})       # 绿色
cmd({"cmd":"lcd","act":"print","row":0,"text":"  I am HAPPY!"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"   \\(^o^)/"})
cmd({"cmd":"display","act":"text","value":"-HI-"})
cmd({"cmd":"buzzer","act":"melody","name":"startup"})     # 开机音乐 ~1s
cmd({"cmd":"servo","act":"set","angle":0})
time.sleep(0.3)

# 开心摇头 (像小狗摇尾巴)
for _ in range(4):
    cmd({"cmd":"servo","act":"set","angle":60})
    time.sleep(0.15)
    cmd({"cmd":"servo","act":"set","angle":120})
    time.sleep(0.15)
cmd({"cmd":"servo","act":"set","angle":90})

# ===== Phase 2 (3-7s): Rainbow dance =====
colors = [
    (255,0,0), (255,128,0), (255,255,0),
    (0,255,0), (0,255,255), (0,0,255), (128,0,255)
]
messages = ["  So excited!", " Let's dance!", "  Yay! Woo!", " Life is good!"]
cmd({"cmd":"display","act":"colon","on":True})

for i, (r,g,b) in enumerate(colors):
    cmd({"cmd":"lcd","act":"rgb","r":r,"g":g,"b":b})
    if i % 2 == 0:
        cmd({"cmd":"lcd","act":"print","row":1,"text": messages[i//2 % len(messages)]})
    # 舵机跳舞 - 不同角度
    angle = 45 + (i * 20)
    cmd({"cmd":"servo","act":"set","angle": angle})
    # 数码管倒计时
    cmd({"cmd":"display","act":"number","value": 8888 - i * 1111})
    # 每个颜色配一个音
    notes = [523, 587, 659, 698, 784, 880, 988]
    cmd({"cmd":"buzzer","act":"tone","freq": notes[i], "ms": 200})
    time.sleep(0.35)

# ===== Phase 3 (7-11s): Servo sweep show =====
cmd({"cmd":"lcd","act":"rgb","r":255,"g":0,"b":255})     # 紫色
cmd({"cmd":"lcd","act":"print","row":0,"text":"  Watch this!"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"   >====>"})
cmd({"cmd":"display","act":"text","value":"GO  "})
cmd({"cmd":"buzzer","act":"melody","name":"success"})

# 平滑扫描 + 同步显示角度
cmd({"cmd":"servo","act":"set","angle":0})
time.sleep(0.3)
for angle in range(0, 181, 15):
    cmd({"cmd":"servo","act":"set","angle": angle})
    cmd({"cmd":"display","act":"number","value": angle})
    time.sleep(0.1)

time.sleep(0.2)
# 快速回摆
for angle in range(180, -1, -15):
    cmd({"cmd":"servo","act":"set","angle": angle})
    cmd({"cmd":"display","act":"number","value": angle})
    time.sleep(0.1)

# ===== Phase 4 (11-14s): Party mode! =====
cmd({"cmd":"lcd","act":"print","row":0,"text":"* PARTY MODE! *"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"  *  *  *  *"})

# 快速闪烁彩色 + 快速摇摆 + 快速哔哔
party_colors = [(255,0,0),(0,255,0),(0,0,255),(255,255,0),(255,0,255),(0,255,255)]
for i in range(12):
    r,g,b = party_colors[i % len(party_colors)]
    cmd({"cmd":"lcd","act":"rgb","r":r,"g":g,"b":b})
    cmd({"cmd":"servo","act":"set","angle": 60 if i%2==0 else 120})
    cmd({"cmd":"buzzer","act":"tone","freq": 800 + (i%3)*200, "ms": 80})
    cmd({"cmd":"display","act":"number","value": (i+1) * 111})
    time.sleep(0.2)

# ===== Phase 5 (14-15s): Grand finale =====
cmd({"cmd":"servo","act":"set","angle":90})               # 居中
cmd({"cmd":"lcd","act":"rgb","r":0,"g":255,"b":128})      # 青绿色
cmd({"cmd":"lcd","act":"print","row":0,"text":"  Thank You!"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"    <3 <3"})
cmd({"cmd":"display","act":"text","value":"LOVE"})
cmd({"cmd":"buzzer","act":"melody","name":"success"})
time.sleep(1.0)

# 最终状态
cmd({"cmd":"lcd","act":"rgb","r":0,"g":0,"b":255})        # 蓝色
cmd({"cmd":"lcd","act":"print","row":0,"text":"Hi Min I'm ready"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"Feed me token!"})
cmd({"cmd":"display","act":"text","value":"8888"})

sock.close()
print("Happy show complete!")
