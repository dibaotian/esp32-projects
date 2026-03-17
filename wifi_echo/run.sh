#!/bin/bash
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

# ============================================================
# WiFi Echo 项目 - 运行 (连接 ESP32 WiFi + 启动客户端)
# 用法: ./run.sh
# ============================================================

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ESP32_IP="192.168.4.1"
ESP32_PORT="3333"

echo "=== ESP32 WiFi Echo - 运行 ==="
echo ""

# Step 1: 配置双网络 (有线 Internet + WiFi ESP32)
if ping -c 1 -W 1 "$ESP32_IP" &>/dev/null; then
    info "ESP32 ($ESP32_IP) 已可达，跳过网络配置"
else
    info "配置网络连接..."
    bash "$SCRIPT_DIR/../setup_dual_network.sh"
fi

# Step 2: 验证 ESP32 TCP 服务可用
echo ""
info "检查 ESP32 TCP 服务..."
if python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(3)
s.connect(('$ESP32_IP', $ESP32_PORT))
s.close()
" 2>/dev/null; then
    info "TCP 端口 $ESP32_PORT 可用"
else
    echo ""
    warn "TCP 连接失败，可能原因:"
    echo "  1. ESP32 未烧录 wifi_echo 固件 → 运行 ./build_flash.sh"
    echo "  2. ESP32 未启动 → 检查供电和串口日志 ./monitor.sh"
    exit 1
fi

# Step 3: 启动交互式客户端
echo ""
info "启动客户端..."
echo ""
python3 "$SCRIPT_DIR/wifi_client.py" "$ESP32_IP" "$ESP32_PORT"
