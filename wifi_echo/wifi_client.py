#!/usr/bin/env python3
"""
ESP32 WiFi 控制客户端 (JSON 协议)

使用方法:
  1. 连接 WiFi: ESP32-Control (密码: esp32ctrl)
  2. 运行: python3 wifi_client.py
  3. 输入 JSON 命令或快捷命令, 输入 quit 退出

JSON 协议:
  {"cmd":"buzzer", "act":"beep", "count":3}
  {"cmd":"servo",  "act":"set",  "angle":90}
  {"cmd":"sys",    "act":"help"}

快捷命令 (自动转为 JSON):
  beep 3              → buzzer.beep
  tone 1000 500       → buzzer.tone
  melody startup      → buzzer.melody
  volume 30           → buzzer.volume
  servo 90            → servo.set
  smooth 180 60       → servo.smooth
  sweep 0 180 45 3    → servo.sweep
  angle               → servo.get
  stop                → servo.stop
  lcd Hello           → lcd.print row0
  lcd2 World          → lcd.print row1
  rgb 255 0 0         → lcd.rgb
  lclear              → lcd.clear
  help                → sys.help

可选参数:
  python3 wifi_client.py [IP] [PORT]
  默认: 192.168.4.1:3333
"""

import json
import socket
import sys
import threading

ESP32_IP = "192.168.4.1"
ESP32_PORT = 3333


def shortcut_to_json(text):
    """将快捷命令转为 JSON 字符串, 不匹配则返回 None"""
    parts = text.strip().split()
    if not parts:
        return None
    cmd = parts[0].lower()

    try:
        if cmd == "beep" and len(parts) >= 2:
            return json.dumps({"cmd": "buzzer", "act": "beep", "count": int(parts[1])})
        if cmd == "tone" and len(parts) >= 3:
            return json.dumps({"cmd": "buzzer", "act": "tone", "freq": int(parts[1]), "ms": int(parts[2])})
        if cmd == "melody" and len(parts) >= 2:
            return json.dumps({"cmd": "buzzer", "act": "melody", "name": parts[1]})
        if cmd == "volume" and len(parts) >= 2:
            return json.dumps({"cmd": "buzzer", "act": "volume", "value": int(parts[1])})
        if cmd == "servo" and len(parts) >= 2:
            return json.dumps({"cmd": "servo", "act": "set", "angle": float(parts[1])})
        if cmd == "smooth" and len(parts) >= 3:
            return json.dumps({"cmd": "servo", "act": "smooth", "angle": float(parts[1]), "speed": float(parts[2])})
        if cmd == "sweep" and len(parts) >= 4:
            count = int(parts[4]) if len(parts) >= 5 else 1
            return json.dumps({"cmd": "servo", "act": "sweep",
                               "start": float(parts[1]), "end": float(parts[2]),
                               "speed": float(parts[3]), "count": count})
        if cmd == "angle":
            return json.dumps({"cmd": "servo", "act": "get"})
        if cmd == "stop":
            return json.dumps({"cmd": "servo", "act": "stop"})
        if cmd == "show" and len(parts) >= 2:
            return json.dumps({"cmd": "display", "act": "text", "value": parts[1]})
        if cmd == "num" and len(parts) >= 2:
            return json.dumps({"cmd": "display", "act": "number", "value": int(parts[1])})
        if cmd == "bright" and len(parts) >= 2:
            return json.dumps({"cmd": "display", "act": "bright", "value": int(parts[1])})
        if cmd == "clear":
            return json.dumps({"cmd": "display", "act": "clear"})
        if cmd == "colon":
            on = parts[1].lower() in ("on", "1", "true") if len(parts) >= 2 else True
            return json.dumps({"cmd": "display", "act": "colon", "on": on})
        if cmd == "lcd" and len(parts) >= 2:
            return json.dumps({"cmd": "lcd", "act": "print", "row": 0, "text": " ".join(parts[1:])})
        if cmd == "lcd2" and len(parts) >= 2:
            return json.dumps({"cmd": "lcd", "act": "print", "row": 1, "text": " ".join(parts[1:])})
        if cmd == "rgb" and len(parts) >= 4:
            return json.dumps({"cmd": "lcd", "act": "rgb", "r": int(parts[1]), "g": int(parts[2]), "b": int(parts[3])})
        if cmd == "lclear":
            return json.dumps({"cmd": "lcd", "act": "clear"})
        if cmd == "help":
            return json.dumps({"cmd": "sys", "act": "help"})
    except (ValueError, IndexError):
        return None
    return None


def receive_loop(sock):
    """后台线程: 持续接收 ESP32 回复"""
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                print("\n[连接断开]")
                break
            text = data.decode("utf-8", errors="replace").strip()
            # 尝试美化 JSON 输出
            try:
                obj = json.loads(text)
                status = obj.get("status", "")
                msg = obj.get("msg", "")
                extra = {k: v for k, v in obj.items() if k not in ("status", "cmd", "act", "msg")}
                icon = "✓" if status == "ok" else "✗"
                line = f"{icon} [{obj.get('cmd','')}.{obj.get('act','')}] {msg}"
                if extra:
                    line += f"  {extra}"
                print(f"\n<< {line}")
            except json.JSONDecodeError:
                print(f"\n<< {text}")
            print(">> ", end="", flush=True)
    except OSError:
        pass


def main():
    ip = sys.argv[1] if len(sys.argv) > 1 else ESP32_IP
    port = int(sys.argv[2]) if len(sys.argv) > 2 else ESP32_PORT

    print(f"正在连接 ESP32 ({ip}:{port})...")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect((ip, port))
    except (socket.timeout, ConnectionRefusedError, OSError) as e:
        print(f"连接失败: {e}")
        print("\n请确认:")
        print("  1. 已连接 WiFi: ESP32-Control (密码: esp32ctrl)")
        print("  2. ESP32 已启动并运行固件")
        print(f"  3. 可以 ping 通: ping {ip}")
        sys.exit(1)

    sock.settimeout(None)
    print("已连接! 输入 JSON 命令或快捷命令, 'help' 查看帮助, 'quit' 退出\n")
    print("快捷命令: beep 3 | servo 90 | lcd Hello | rgb 255 0 0 | help\n")

    recv_thread = threading.Thread(target=receive_loop, args=(sock,), daemon=True)
    recv_thread.start()

    try:
        while True:
            msg = input(">> ")
            if msg.lower() == "quit":
                break
            if not msg:
                continue

            # 如果已经是 JSON, 直接发送
            if msg.strip().startswith("{"):
                sock.send((msg + "\n").encode("utf-8"))
            else:
                # 尝试快捷命令转换
                j = shortcut_to_json(msg)
                if j:
                    sock.send((j + "\n").encode("utf-8"))
                else:
                    # 纯文本, 直接发送 (走回声)
                    sock.send((msg + "\n").encode("utf-8"))
    except (KeyboardInterrupt, EOFError):
        print("\n退出...")
    finally:
        sock.close()
        print("连接已关闭")


if __name__ == "__main__":
    main()
