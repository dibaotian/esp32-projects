#!/usr/bin/env python3
"""
Ubuntu TCP 客户端 - 连接 ESP32 WiFi 控制服务器
使用方法:
  1. 连接 WiFi: ESP32-Control (密码: esp32ctrl)
  2. 运行: python3 wifi_client.py
  3. 输入消息回车发送, 输入 quit 退出

可选参数:
  python3 wifi_client.py [IP] [PORT]
  默认: 192.168.4.1:3333
"""

import socket
import sys
import threading

ESP32_IP = "192.168.4.1"
ESP32_PORT = 3333


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
        print("  2. ESP32 已启动并运行 wifi_echo 固件")
        print(f"  3. 可以 ping 通: ping {ip}")
        sys.exit(1)

    sock.settimeout(None)
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
