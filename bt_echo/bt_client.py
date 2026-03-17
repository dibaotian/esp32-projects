#!/usr/bin/env python3
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

"""
Ubuntu 蓝牙客户端 - 连接 ESP32-Echo 蓝牙回声服务器
使用方法:
  1. 先配对: bluetoothctl 里 scan on -> pair <MAC> -> trust <MAC>
  2. 运行: python3 bt_client.py
  3. 输入消息回车发送, 输入 quit 退出

依赖安装:
  sudo apt-get install -y bluetooth bluez python3-dbus
  pip3 install --break-system-packages pybluez2
"""

import socket
import sys
import threading

# ESP32 蓝牙 MAC 地址 (烧录时日志显示的蓝牙 MAC)
ESP32_BT_MAC = "FC:F5:C4:16:24:A6"
SPP_UUID = "00001101-0000-1000-8000-00805f9b34fb"


def receive_loop(sock):
    """后台线程: 持续接收 ESP32 回复"""
    try:
        while True:
            data = sock.recv(1024)
            if not data:
                print("\n[连接断开]")
                break
            print(f"\n<< ESP32: {data.decode('utf-8', errors='replace')}")
            print(">> ", end="", flush=True)
    except OSError:
        pass


def main():
    mac = ESP32_BT_MAC
    if len(sys.argv) > 1:
        mac = sys.argv[1]

    print(f"正在连接 ESP32-Echo ({mac})...")

    sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
    try:
        sock.connect((mac, 1))  # RFCOMM channel 1
    except OSError as e:
        print(f"连接失败: {e}")
        print("\n请确认:")
        print("  1. 已插入 USB 蓝牙适配器")
        print("  2. 已配对 ESP32:")
        print(f"     bluetoothctl")
        print(f"     > scan on")
        print(f"     > pair {mac}")
        print(f"     > trust {mac}")
        print(f"     > quit")
        sys.exit(1)

    print("已连接! 输入消息回车发送, 输入 'quit' 退出\n")

    # 启动接收线程
    recv_thread = threading.Thread(target=receive_loop, args=(sock,), daemon=True)
    recv_thread.start()

    try:
        while True:
            msg = input(">> ")
            if msg.lower() == "quit":
                break
            if msg:
                sock.send(msg.encode("utf-8"))
    except (KeyboardInterrupt, EOFError):
        print("\n退出...")
    finally:
        sock.close()
        print("连接已关闭")


if __name__ == "__main__":
    main()
