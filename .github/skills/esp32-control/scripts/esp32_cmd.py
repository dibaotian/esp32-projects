#!/usr/bin/env python3
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

"""
ESP32 控制辅助脚本 - 供 Agent 调用

用法:
  python3 esp32_cmd.py '{"cmd":"servo","act":"set","angle":90}'
  python3 esp32_cmd.py '{"cmd":"display","act":"number","value":42}'
  python3 esp32_cmd.py '{"cmd":"buzzer","act":"beep","count":3}'

多条命令 (用 ; 分隔):
  python3 esp32_cmd.py '{"cmd":"servo","act":"set","angle":0}' '{"cmd":"display","act":"number","value":0}'
"""

import socket
import json
import sys
import time

ESP32_IP = "192.168.4.1"
ESP32_PORT = 3333
TIMEOUT = 5


def send_commands(commands):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((ESP32_IP, ESP32_PORT))
        sock.settimeout(TIMEOUT)

        for cmd_str in commands:
            # Validate JSON
            cmd = json.loads(cmd_str)
            msg = json.dumps(cmd) + "\n"
            sock.send(msg.encode("utf-8"))
            time.sleep(0.3)

            try:
                data = sock.recv(4096).decode("utf-8").strip()
                first_line = data.split("\n")[0]
                resp = json.loads(first_line)
                status = resp.get("status", "?")
                icon = "OK" if status == "ok" else "ERR"
                print(f"[{icon}] {resp.get('cmd','')}.{resp.get('act','')}: {resp.get('msg','')}")
            except socket.timeout:
                print(f"[TIMEOUT] No response for: {cmd_str}")
            except json.JSONDecodeError:
                print(f"[RAW] {data}")
    except ConnectionRefusedError:
        print("[ERROR] Cannot connect to ESP32. Is WiFi connected?")
        sys.exit(1)
    finally:
        sock.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    send_commands(sys.argv[1:])
