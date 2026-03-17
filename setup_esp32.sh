#!/bin/bash
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

# ============================================================
# ESP32 开发环境一键安装脚本
# 系统: Ubuntu 24.04 LTS
# 芯片: ESP32-D0WDQ6 (双核, Wi-Fi + BT, 240MHz)
# 串口芯片: CP2102 (Silicon Labs)
# 日期: 2026-03-16
# ============================================================

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ============================================================
# 1. 安装系统依赖
# ============================================================
info "Step 1: 安装系统依赖包..."
sudo apt-get update
sudo apt-get install -y \
    git wget flex bison gperf \
    python3 python3-pip python3-venv \
    cmake ninja-build ccache \
    libffi-dev libssl-dev \
    dfu-util libusb-1.0-0

info "系统依赖安装完成。"

# ============================================================
# 2. 安装 esptool
# ============================================================
info "Step 2: 安装 esptool..."
pip3 install --break-system-packages esptool
sudo pip3 install --break-system-packages esptool

info "esptool 安装完成。"

# ============================================================
# 3. 添加用户到 dialout 组 (串口权限)
# ============================================================
if groups "$USER" | grep -q dialout; then
    info "用户已在 dialout 组中。"
else
    info "Step 3: 添加用户到 dialout 组 (串口访问权限)..."
    sudo usermod -aG dialout "$USER"
    warn "dialout 组已添加，需要注销重新登录才能生效。"
    warn "在此之前，请使用 sudo 访问串口。"
fi

# ============================================================
# 4. 检测 ESP32 板子
# ============================================================
info "Step 4: 检测串口设备..."

SERIAL_PORT=""
for port in /dev/ttyUSB* /dev/ttyACM*; do
    if [ -e "$port" ]; then
        SERIAL_PORT="$port"
        break
    fi
done

if [ -n "$SERIAL_PORT" ]; then
    info "检测到串口: $SERIAL_PORT"
    info "尝试读取芯片信息..."
    sudo python3 -m esptool --port "$SERIAL_PORT" chip-id || warn "无法读取芯片信息，请检查板子连接。"
else
    warn "未检测到串口设备，请确认 ESP32 板子已通过 USB 连接。"
fi

# ============================================================
# 5. 显示 USB 设备
# ============================================================
info "USB 串口设备列表:"
lsusb 2>/dev/null | grep -iE "cp210|ch340|ch341|ftdi|silicon|espressif|uart|serial|bridge" || echo "  (未发现已知串口芯片)"

echo ""
info "========================================="
info "  ESP32 基础开发环境安装完成!"
info "========================================="
echo ""
echo "  串口设备: ${SERIAL_PORT:-未检测到}"
echo "  esptool:  $(python3 -m esptool version 2>/dev/null | head -1 || echo '未安装')"
echo ""
echo "  后续步骤 (可选):"
echo "  - 安装 ESP-IDF:  参考 README_ESP32_SETUP.md"
echo "  - 安装 Arduino:  参考 README_ESP32_SETUP.md"
echo "  - 安装 PlatformIO: pip3 install platformio"
echo ""
warn "如果刚添加了 dialout 组，请注销并重新登录!"
