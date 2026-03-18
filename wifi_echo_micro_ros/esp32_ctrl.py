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
    buzzer twinkle/birthday/jingle/mario/anthem  播放歌曲 (在线发送到ESP32播放)
    display <数字>         数码管显示 0~9999
    display off            数码管清屏
    lcd <文字>             LCD 显示文字
    scan fast              舵机快速扫描 0→180→0 (10°步进)
    scan slow              舵机慢速扫描 0→180→0 (1°步进)
    demo                   运行演示序列
    status                 查看心跳和舵机状态
    help                   显示帮助
    quit                   退出
"""

import sys
import time
import threading

import math

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from std_msgs.msg import Float32, Int32, String

# 乐曲数据 (freq_hz, duration_ms) — 通过 melody_cmd 在线发送, ESP32 本地播放
# 格式: "P<pause>|freq,dur;freq,dur;..."  (P=音符间隔ms)
MELODIES = {
    'twinkle': {
        'name': '小星星 ⭐',
        'pause': 30,
        'notes': [
            (262,300),(262,300),(392,300),(392,300),(440,300),(440,300),(392,500),
            (349,300),(349,300),(330,300),(330,300),(294,300),(294,300),(262,500),
            (392,300),(392,300),(349,300),(349,300),(330,300),(330,300),(294,500),
            (392,300),(392,300),(349,300),(349,300),(330,300),(330,300),(294,500),
            (262,300),(262,300),(392,300),(392,300),(440,300),(440,300),(392,500),
            (349,300),(349,300),(330,300),(330,300),(294,300),(294,300),(262,600),
        ],
    },
    'birthday': {
        'name': '生日快乐 🎂',
        'pause': 30,
        'notes': [
            (262,200),(262,200),(294,400),(262,400),(349,400),(330,600),
            (262,200),(262,200),(294,400),(262,400),(392,400),(349,600),
            (262,200),(262,200),(523,400),(440,400),(349,400),(330,400),(294,600),
            (466,200),(466,200),(440,400),(349,400),(392,400),(349,600),
        ],
    },
    'jingle': {
        'name': '铃儿响叮当 🔔',
        'pause': 30,
        'notes': [
            (330,250),(330,250),(330,500),
            (330,250),(330,250),(330,500),
            (330,250),(392,250),(262,250),(294,250),(330,700),
            (349,250),(349,250),(349,250),(349,250),
            (349,250),(330,250),(330,250),(330,125),(330,125),
            (330,250),(294,250),(294,250),(330,250),(294,500),(392,500),
        ],
    },
    'mario': {
        'name': '超级马里奥 🍄',
        'pause': 20,
        'notes': [
            (659,150),(659,150),(0,150),(659,150),(0,150),(523,150),(659,300),
            (784,300),(0,300),(392,300),(0,300),
            (523,300),(0,150),(392,300),(0,150),(330,300),
            (0,150),(440,300),(494,300),(466,150),(440,300),
            (392,200),(659,200),(784,200),(880,300),(698,150),(784,150),
            (0,150),(659,300),(523,150),(587,150),(494,300),
        ],
    },
    'anthem': {
        'name': '义勇军进行曲 🇨🇳',
        'pause': 30,
        'notes': [
            (392,300),(392,300),(392,300),(330,450),(0,50),(392,200),
            (440,300),(440,300),(440,300),(392,450),(0,50),(440,200),
            (392,600),(0,100),
            (392,200),(330,200),(392,200),(440,200),(392,300),
            (330,300),(262,300),(392,600),(0,200),
            (392,300),(392,300),(440,450),(0,50),(392,200),
            (330,200),(392,300),(294,600),(0,100),(294,300),(294,300),
            (330,300),(294,300),(262,300),(294,300),(196,600),(0,300),
            (392,300),(392,300),(392,300),(330,450),(0,50),(392,200),
            (440,300),(440,300),(440,300),(392,450),(0,50),(440,200),
            (523,600),(0,100),(523,300),(392,450),(0,50),
            (440,200),(392,200),(330,300),(294,300),(196,300),(262,300),
            (294,600),(0,200),
            (330,450),(0,50),(294,200),(262,300),(294,300),(196,600),(0,200),
            (262,250),(262,250),(262,200),(262,200),(294,200),(330,300),
            (294,450),(0,50),(262,200),(294,200),(196,300),
            (262,600),(0,100),(262,800),
        ],
    },
    'intel': {
        'name': 'Intel 🎵',
        'pause': 0,
        'notes': [
            (988,180),(0,60),(1319,180),(0,60),(988,180),(0,60),(659,180),(0,60),(784,400),
        ],
    },
}


def melody_to_cmd(name: str) -> str:
    """将乐曲名转为 melody_cmd 格式字符串"""
    m = MELODIES.get(name)
    if not m:
        return ''
    notes_str = ';'.join(f'{f},{d}' for f, d in m['notes'])
    return f"P{m['pause']}|{notes_str}"


def melody_to_cmd_tuned(name: str, pitch: int = 0, tempo: float = 1.0,
                        pause: int = -1) -> str:
    """将乐曲名转为 melody_cmd 格式字符串, 支持调参

    Args:
        name:  乐曲名 (MELODIES key)
        pitch: 移调半音数 (+12=升一个八度, -12=降一个八度)
        tempo: 速度倍率 (>1 加快, <1 减慢, 例: 0.8=慢20%)
        pause: 音符间隔 ms (-1 表示使用原始值)
    """
    m = MELODIES.get(name)
    if not m:
        return ''
    factor = 2.0 ** (pitch / 12.0)  # 半音移调系数
    p = pause if pause >= 0 else m['pause']
    parts = []
    for freq, dur in m['notes']:
        # 移调: 频率乘以半音系数 (0=静音不变)
        f = int(round(freq * factor)) if freq > 0 else 0
        # 限制频率范围 (ESP32 蜂鸣器 20~20000 Hz)
        if f > 0:
            f = max(20, min(20000, f))
        # 速度: 时值除以 tempo (tempo>1 缩短时值=加快)
        d = max(10, int(round(dur / tempo)))
        parts.append(f'{f},{d}')
    return f"P{p}|{';'.join(parts)}"


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
        self.pub_melody = self.create_publisher(String, '/esp32/melody_cmd', pub_qos)

        # 订阅状态 (BEST_EFFORT 匹配 micro-ROS publisher)
        self._heartbeat = None
        self._servo_state = None
        self.create_subscription(Int32, '/esp32/heartbeat', self._on_heartbeat, sub_qos)
        self.create_subscription(Float32, '/esp32/servo_state', self._on_servo_state, sub_qos)

    def wait_for_subscribers(self, timeout=5.0):
        """等待 ESP32 subscriber 被发现 (DDS discovery)"""
        pubs = [self.pub_servo, self.pub_buzzer, self.pub_display, self.pub_lcd, self.pub_melody]
        names = ['servo', 'buzzer', 'display', 'lcd', 'melody']
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
        msg.data = angle if angle < 0.0 else max(0.0, min(180.0, angle))
        self.pub_servo.publish(msg)
        if angle < 0.0:
            print(f'  → 舵机: 扫描命令 ({angle})')
        else:
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

    def play_melody(self, name: str):
        """通过 melody_cmd 在线发送乐曲到 ESP32 播放"""
        cmd = melody_to_cmd(name)
        if not cmd:
            print(f'  未知乐曲: {name}')
            return
        msg = String()
        msg.data = cmd
        self.pub_melody.publish(msg)
        print(f'  → 乐曲: {MELODIES[name]["name"]}')

    def play_melody_tuned(self, name: str, pitch: int = 0,
                          tempo: float = 1.0, pause: int = -1):
        """通过 melody_cmd 发送调参后的乐曲"""
        cmd = melody_to_cmd_tuned(name, pitch, tempo, pause)
        if not cmd:
            print(f'  未知乐曲: {name}')
            return
        msg = String()
        msg.data = cmd
        self.pub_melody.publish(msg)
        info = MELODIES[name]['name']
        params = []
        if pitch != 0:
            params.append(f'pitch={pitch:+d}')
        if tempo != 1.0:
            params.append(f'tempo={tempo:.2f}')
        if pause >= 0:
            params.append(f'pause={pause}ms')
        pstr = f' ({" ".join(params)})' if params else ''
        print(f'  → 乐曲: {info}{pstr}')

    def scan(self, slow=False):
        """舵机扫描 (ESP32 本地执行, 零网络抖动)"""
        val = -1.0 if slow else -2.0
        self.servo(val)

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
  buzzer twinkle     小星星 ⭐
  buzzer birthday    生日快乐 🎂
  buzzer jingle      铃儿响叮当 🔔
  buzzer mario       超级马里奥 🍄
  buzzer anthem      义勇军进行曲 🇨🇳
  buzzer intel       Intel 🎵
  tune <曲名> [p=N] [t=N] [g=N]  调参播放
    p=移调半音 (+12升八度, -12降八度, 默认0)
    t=速度倍率 (0.5慢一倍, 2.0快一倍, 默认1.0)
    g=音符间隔ms (默认用原曲值)
    例: tune anthem p=-2 t=0.85 g=20
  display <数字>     数码管 0~9999      例: display 2026
  display off        数码管清屏
  lcd <文字>         LCD 显示           例: lcd Hello ROS2
  scan fast          舵机快速扫描 0→180→0 (10°步进)
  scan slow          舵机慢速扫描 0→180→0 (1°步进)
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
                elif arg.lower() == 'twinkle':
                    ctrl.play_melody('twinkle')
                elif arg.lower() == 'birthday':
                    ctrl.play_melody('birthday')
                elif arg.lower() == 'jingle':
                    ctrl.play_melody('jingle')
                elif arg.lower() == 'mario':
                    ctrl.play_melody('mario')
                elif arg.lower() == 'anthem':
                    ctrl.play_melody('anthem')
                elif arg.lower() == 'intel':
                    ctrl.play_melody('intel')
                else:
                    try:
                        n = int(arg)
                        ctrl.buzzer(n)
                        print(f'  → 蜂鸣器: beep × {n}')
                    except ValueError:
                        print('  用法: buzzer <次数>|stop|startup|success|error|twinkle|birthday|jingle|mario|anthem|intel')
            elif cmd == 'tone':
                try:
                    freq = int(arg)
                    ctrl.buzzer(freq)
                    print(f'  → 蜂鸣器: tone {freq} Hz')
                except ValueError:
                    print('  用法: tone <频率Hz>  例: tone 1000')
            elif cmd == 'tune':
                # tune <name> [p=N] [t=N] [g=N]
                tparts = arg.split()
                if not tparts:
                    print('  用法: tune <曲名> [p=移调] [t=速度] [g=间隔]')
                    print(f'  可选曲名: {", ".join(MELODIES.keys())}')
                else:
                    tname = tparts[0].lower()
                    if tname not in MELODIES:
                        print(f'  未知乐曲: {tname}')
                        print(f'  可选: {", ".join(MELODIES.keys())}')
                    else:
                        tp, tt, tg = 0, 1.0, -1
                        for tp_arg in tparts[1:]:
                            if tp_arg.startswith('p='):
                                try: tp = int(tp_arg[2:])
                                except ValueError: pass
                            elif tp_arg.startswith('t='):
                                try: tt = float(tp_arg[2:])
                                except ValueError: pass
                            elif tp_arg.startswith('g='):
                                try: tg = int(tp_arg[2:])
                                except ValueError: pass
                        ctrl.play_melody_tuned(tname, pitch=tp, tempo=tt, pause=tg)
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
                sub = arg.lower().strip()
                if sub == 'slow':
                    ctrl.scan(slow=True)
                else:
                    ctrl.scan(slow=False)
                    if sub and sub not in ('fast', 'faster'):
                        print(f'  (未知子命令 "{sub}", 默认 fast)')
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
