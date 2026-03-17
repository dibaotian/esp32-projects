#!/usr/bin/env python3
"""ESP32 Sad Expression - 15 second melancholy show"""

import socket, json, time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("192.168.4.1", 3333))
sock.settimeout(2)

def cmd(d):
    sock.send((json.dumps(d) + "\n").encode())
    time.sleep(0.08)
    try: sock.recv(4096)
    except: pass

# ===== Phase 1 (0-3s): Something is wrong... =====
cmd({"cmd":"lcd","act":"clear"})
cmd({"cmd":"lcd","act":"rgb","r":0,"g":0,"b":40})         # 暗蓝，像深夜
cmd({"cmd":"lcd","act":"print","row":0,"text":"  I feel sad..."})
cmd({"cmd":"lcd","act":"print","row":1,"text":"    (T_T)"})
cmd({"cmd":"display","act":"text","value":"    "})          # 空白
cmd({"cmd":"servo","act":"set","angle":90})
time.sleep(0.5)

# 舵机缓慢低头 (90° → 20°)，像垂头丧气
for angle in range(90, 19, -2):
    cmd({"cmd":"servo","act":"set","angle": angle})
    time.sleep(0.05)

# 一声叹息 - 下降音调
cmd({"cmd":"buzzer","act":"tone","freq":600,"ms":300})
time.sleep(0.4)
cmd({"cmd":"buzzer","act":"tone","freq":400,"ms":400})
time.sleep(0.5)
cmd({"cmd":"buzzer","act":"tone","freq":250,"ms":600})
time.sleep(0.8)

# ===== Phase 2 (3-7s): Tears falling... =====
cmd({"cmd":"lcd","act":"print","row":0,"text":"Why does it hurt"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"   so much..."})

# 眼泪：背光从蓝慢慢变暗再亮，像泪珠滑落
# 数码管逐位亮起又熄灭，像眼泪一滴一滴
tear_patterns = [
    "   1", "  1 ", " 1  ", "1   ",
    "   1", "  1 ", " 1  ", "1   ",
]
for i, pat in enumerate(tear_patterns):
    # 背光呼吸 - 蓝色脉动
    brightness = 20 + abs(40 - (i * 10) % 80)
    cmd({"cmd":"lcd","act":"rgb","r":0,"g":0,"b":brightness})
    cmd({"cmd":"display","act":"text","value": pat})
    # 水滴声
    cmd({"cmd":"buzzer","act":"tone","freq":1200 - i*50,"ms":60})
    time.sleep(0.45)

# ===== Phase 3 (7-10s): The weight of sadness =====
cmd({"cmd":"lcd","act":"rgb","r":30,"g":0,"b":30})        # 暗紫色，压抑
cmd({"cmd":"lcd","act":"print","row":0,"text":" Everything is"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"   falling..."})

# 舵机挣扎着想抬头，但又放弃，反复3次
for attempt in range(3):
    # 试图抬起
    for a in range(20, 50, 3):
        cmd({"cmd":"servo","act":"set","angle": a})
        time.sleep(0.03)
    # 又掉下去，越来越低
    target = 15 - attempt * 5
    for a in range(50, max(target, 0), -2):
        cmd({"cmd":"servo","act":"set","angle": a})
        time.sleep(0.03)
    # 痛苦的声音 - 越来越低
    cmd({"cmd":"buzzer","act":"tone","freq":300 - attempt*50,"ms":500})
    cmd({"cmd":"display","act":"number","value": attempt + 1})
    time.sleep(0.6)

# ===== Phase 4 (10-13s): Deepest despair =====
cmd({"cmd":"lcd","act":"rgb","r":50,"g":0,"b":0})         # 暗红，痛苦
cmd({"cmd":"lcd","act":"print","row":0,"text":"  I can't take"})
cmd({"cmd":"lcd","act":"print","row":1,"text":"  this anymore"})
cmd({"cmd":"servo","act":"set","angle":0})                  # 完全低头

# 心跳声越来越慢 - 像生命在流逝
heartbeats = [
    (400, 120, 0.5),   # 正常心跳
    (380, 100, 0.7),   # 变慢
    (350, 80,  0.9),   # 更慢
    (300, 60,  1.2),   # 快停了
]
for freq, ms, gap in heartbeats:
    cmd({"cmd":"buzzer","act":"tone","freq":freq,"ms":ms})
    # 数码管闪一下，像心电图
    cmd({"cmd":"display","act":"text","value":"----"})
    time.sleep(0.15)
    cmd({"cmd":"display","act":"text","value":"    "})
    # 背光随心跳微微亮一下
    cmd({"cmd":"lcd","act":"rgb","r":80,"g":0,"b":0})
    time.sleep(0.1)
    cmd({"cmd":"lcd","act":"rgb","r":20,"g":0,"b":0})
    time.sleep(gap)

# ===== Phase 5 (13-15s): A tiny hope... =====
cmd({"cmd":"display","act":"text","value":"    "})
time.sleep(0.5)

# 最后背光从黑暗中慢慢亮起一点点暖光
cmd({"cmd":"lcd","act":"print","row":0,"text":"  But maybe..."})
cmd({"cmd":"lcd","act":"print","row":1,"text":" tomorrow... ?"})

for brightness in range(0, 60, 3):
    cmd({"cmd":"lcd","act":"rgb","r":brightness,"g":brightness//3,"b":0})
    time.sleep(0.04)

# 舵机微微抬头，像鼓起最后一点勇气
for a in range(0, 45, 2):
    cmd({"cmd":"servo","act":"set","angle": a})
    time.sleep(0.04)

# 一个微弱的音符
cmd({"cmd":"buzzer","act":"tone","freq":523,"ms":800})
cmd({"cmd":"display","act":"text","value":"-  -"})
time.sleep(1.0)

# 结束 - 停在微弱的暖光中
cmd({"cmd":"lcd","act":"rgb","r":40,"g":15,"b":0})
cmd({"cmd":"lcd","act":"print","row":0,"text":"    (...)"})
cmd({"cmd":"lcd","act":"print","row":1,"text":""})
cmd({"cmd":"display","act":"text","value":"    "})
cmd({"cmd":"servo","act":"set","angle":30})

sock.close()
print("Sad show complete...")
