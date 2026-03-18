#!/usr/bin/env python3
"""
ESP32 micro-ROS 交互式控制器

保持 ROS 2 publisher 持久连接, 跳过每次 DDS 发现的开销。
命令延迟从 ~2-3 秒降为 <100ms。

用法:
    source /opt/ros/jazzy/setup.bash
    python3 esp32_ctrl.py

命令:
    servo <角度>           舵机转到 0~180°
    buzzer <次数>          蜂鸣器响 1~10 次
    tone <频率>            播放指定频率 Hz
    buzzer stop            停止蜂鸣器
    buzzer startup/success/error  播放音效
    display <数字>         数码管显示 0~9999
    display off            数码管清屏
    lcd <文字>             LCD 显示文字
    scan                   舵机扫描 0→180→0
    demo                   运行演示序列
    status                 查看心跳和舵机状态
    help                   显示帮助
    quit                   退出
"""

import sys
import time
import threading

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from std_msgs.msg import Float32, Int32, String


class ESP32Controller(Node):
    def __init__(self):
        super().__init__('esp32_ctrl')

        # micro-ROS subscribers default to RELIABLE
        pub_qos = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )
        # heartbeat/servo_state from micro-ROS use BEST_EFFORT
        sub_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
        )

        # 持久 publisher — 创建一次, 重复使用 (RELIABLE 匹配 micro-ROS subscriber)
        self.pub_servo = self.create_publisher(Float32, '/esp32/servo_cmd', pub_qos)
        self.pub_buzzer = self.create_publisher(Int32, '/esp32/buzzer_cmd', pub_qos)
        self.pub_display = self.create_publisher(Int32, '/esp32/display_cmd', pub_qos)
        self.pub_lcd = self.create_publisher(String, '/esp32/lcd_cmd', pub_qos)

        # 订阅状态 (BEST_EFFORT 匹配 micro-ROS publisher)
        self._heartbeat = None
        self._servo_state = None
        self.create_subscription(Int32, '/esp32/heartbeat', self._on_heartbeat, sub_qos)
        self.create_subscription(Float32, '/esp32/servo_state', self._on_servo_state, sub_qos)

    def wait_for_subscribers(self, timeout=5.0):
        """等待 ESP32 subscriber 被发现 (DDS discovery)"""
        pubs = [self.pub_servo, self.pub_buzzer, self.pub_display, self.pub_lcd]
        names = ['servo', 'buzzer', 'display', 'lcd']
        t0 = time.time()
        while time.time() - t0 < timeout:
            counts = [p.get_subscription_count() for p in pubs]
            if all(c > 0 for c in counts):
                return True
            time.sleep(0.1)
        # 打印未匹配的 topic
        counts = [p.get_subscription_count() for p in pubs]
        missing = [n for n, c in zip(names, counts) if c == 0]
        if missing:
            print(f'  警告: 未发现 ESP32 订阅者: {", ".join(missing)} (命令可能丢失)')
        return False

    def _on_heartbeat(self, msg):
        self._heartbeat = msg.data

    def _on_servo_state(self, msg):
        self._servo_state = msg.data

    def servo(self, angle: float):
        msg = Float32()
        msg.data = max(0.0, min(180.0, angle))
        self.pub_servo.publish(msg)
        print(f'  → 舵机: {msg.data:.1f}°')

    def buzzer(self, val: int):
        msg = Int32()
        msg.data = val
        self.pub_buzzer.publish(msg)

    def display(self, val: int):
        msg = Int32()
        msg.data = val
        self.pub_display.publish(msg)
        if val >= 0:
            print(f'  → 数码管: {val}')
        else:
            print('  → 数码管: 清屏')

    def lcd(self, text: str):
        msg = String()
        msg.data = text
        self.pub_lcd.publish(msg)
        print(f'  → LCD: {text}')

    def scan(self):
        """舵机扫描演示"""
        for angle in range(0, 181, 10):
            self.servo(float(angle))
            time.sleep(0.08)
        for angle in range(180, -1, -10):
            self.servo(float(angle))
            time.sleep(0.08)
        self.servo(90.0)

    def demo(self):
        """运行完整演示"""
        print('--- 演示开始 ---')
        self.lcd('Demo Start!')
        time.sleep(0.3)

        print('舵机扫描...')
        self.scan()
        time.sleep(0.3)

        print('蜂鸣器...')
        self.buzzer(3)
        print('  → 蜂鸣器: beep × 3')
        time.sleep(1.0)

        print('数码管计数...')
        for n in [1, 12, 123, 1234, 2026]:
            self.display(n)
            time.sleep(0.4)

        self.buzzer(-2)
        print('  → 蜂鸣器: 成功音效')
        self.lcd('Demo Done!')
        print('--- 演示结束 ---')

    def status(self):
        hb = self._heartbeat
        sv = self._servo_state
        print(f'  心跳: {hb}s' if hb is not None else '  心跳: (未收到)')
        print(f'  舵机: {sv:.1f}°' if sv is not None else '  舵机: (未收到)')


def print_help():
    print('''
命令:
  servo <角度>       舵机 0~180°        例: servo 90
  buzzer <次数>      蜂鸣器响 N 次      例: buzzer 3
  tone <频率>        播放频率 Hz        例: tone 1000
  buzzer stop        停止蜂鸣器
  buzzer startup     开机音效
  buzzer success     成功音效
  buzzer error       错误音效
  display <数字>     数码管 0~9999      例: display 2026
  display off        数码管清屏
  lcd <文字>         LCD 显示           例: lcd Hello ROS2
  scan               舵机扫描 0→180→0
  demo               运行演示序列
  status             查看 ESP32 状态
  diag               系统诊断 (heap/uptime)
  diag wifi          WiFi 信号/IP
  diag task          任务栈水位
  help               显示本帮助
  quit / exit        退出
''')


def main():
    rclpy.init()
    ctrl = ESP32Controller()

    # 后台 spin (spin_once 循环, 每次短暂释放 GIL 避免阻塞主线程)
    _running = True
    def _spin():
        while _running and rclpy.ok():
            try:
                rclpy.spin_once(ctrl, timeout_sec=0.05)
            except Exception:
                break
    spin_thread = threading.Thread(target=_spin, daemon=True)
    spin_thread.start()

    print('ESP32 micro-ROS 控制器')
    print('等待 DDS 发现 ESP32 节点...', end=' ', flush=True)
    if ctrl.wait_for_subscribers(timeout=5.0):
        print('已就绪!')
    else:
        print('(超时, 部分 topic 未匹配)')
    print('输入 help 查看命令\n')

    try:
        while True:
            try:
                line = input('esp32> ').strip()
            except EOFError:
                break
            if not line:
                continue

            parts = line.split(maxsplit=1)
            cmd = parts[0].lower()
            arg = parts[1] if len(parts) > 1 else ''

            if cmd in ('quit', 'exit', 'q'):
                break
            elif cmd == 'help':
                print_help()
            elif cmd == 'servo':
                try:
                    ctrl.servo(float(arg))
                except ValueError:
                    print('  用法: servo <角度>  例: servo 90')
            elif cmd == 'buzzer':
                if arg.lower() == 'stop':
                    ctrl.buzzer(0)
                    print('  → 蜂鸣器: 停止')
                elif arg.lower() == 'startup':
                    ctrl.buzzer(-1)
                    print('  → 蜂鸣器: 开机音效')
                elif arg.lower() == 'success':
                    ctrl.buzzer(-2)
                    print('  → 蜂鸣器: 成功音效')
                elif arg.lower() == 'error':
                    ctrl.buzzer(-3)
                    print('  → 蜂鸣器: 错误音效')
                else:
                    try:
                        n = int(arg)
                        ctrl.buzzer(n)
                        print(f'  → 蜂鸣器: beep × {n}')
                    except ValueError:
                        print('  用法: buzzer <次数>|stop|startup|success|error')
            elif cmd == 'tone':
                try:
                    freq = int(arg)
                    ctrl.buzzer(freq)
                    print(f'  → 蜂鸣器: tone {freq} Hz')
                except ValueError:
                    print('  用法: tone <频率Hz>  例: tone 1000')
            elif cmd == 'display':
                if arg.lower() == 'off':
                    ctrl.display(-1)
                else:
                    try:
                        ctrl.display(int(arg))
                    except ValueError:
                        print('  用法: display <数字>|off  例: display 2026')
            elif cmd == 'lcd':
                if arg:
                    ctrl.lcd(arg)
                else:
                    print('  用法: lcd <文字>  例: lcd Hello ROS2')
            elif cmd == 'scan':
                ctrl.scan()
            elif cmd == 'demo':
                ctrl.demo()
            elif cmd == 'status':
                ctrl.status()
            elif cmd == 'diag':
                sub = arg.lower().strip()
                if sub == 'wifi':
                    ctrl.display(-11)
                    print('  → 诊断: WiFi (查看 LCD + 串口)')
                elif sub == 'task':
                    ctrl.display(-12)
                    print('  → 诊断: 任务栈 (查看 LCD + 串口)')
                else:
                    ctrl.display(-10)
                    print('  → 诊断: 系统 (查看 LCD + 串口)')
            else:
                print(f'  未知命令: {cmd} (输入 help 查看帮助)')

    except KeyboardInterrupt:
        print()

    _running = False
    spin_thread.join(timeout=1.0)
    try:
        ctrl.destroy_node()
        rclpy.shutdown()
    except Exception:
        pass
    print('已退出')


if __name__ == '__main__':
    main()
