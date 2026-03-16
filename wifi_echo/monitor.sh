#!/bin/bash
# ============================================================
# WiFi Echo 项目 - 串口监控 ESP32 输出
# 用法: ./monitor.sh [PORT]
# 按 Ctrl+] 退出
# ============================================================

PORT="${1:-/dev/ttyUSB0}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== ESP32 串口监控 ==="
echo "  串口: $PORT"
echo "  按 Ctrl+] 退出"
echo "========================"
echo ""

# 优先用 idf.py monitor
if command -v idf.py &>/dev/null; then
    cd "$PROJECT_DIR"
    sudo idf.py -p "$PORT" monitor
else
    # 激活 ESP-IDF
    if [ -f "$IDF_PATH/export.sh" ]; then
        source "$IDF_PATH/export.sh"
        cd "$PROJECT_DIR"
        sudo idf.py -p "$PORT" monitor
    else
        # 回退: 用 Python 简单监控
        sudo python3 -c "
import serial, time, sys
ser = serial.Serial('$PORT', 115200, timeout=1)
ser.dtr = False; ser.rts = True; time.sleep(0.1)
ser.rts = False; time.sleep(0.1); ser.dtr = False
print('监控中... 按 Ctrl+C 退出\n')
try:
    while True:
        line = ser.readline()
        if line:
            print(line.decode('utf-8', errors='replace').rstrip())
except KeyboardInterrupt:
    print('\n退出监控')
finally:
    ser.close()
"
    fi
fi
