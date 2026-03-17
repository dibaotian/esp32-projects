#!/bin/bash
# Copyright (c) 2026 minxie <laba22@163.com>
# All rights reserved.

# ============================================================
# WiFi Echo 项目 - 停止 (断开 ESP32 WiFi 连接)
# 用法: ./stop.sh
# ============================================================

GREEN='\033[0;32m'
NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC} $1"; }

WIFI_IF=$(ip -o link show | awk -F': ' '{print $2}' | grep -E '^wl' | head -1)

echo "=== ESP32 WiFi Echo - 停止 ==="

if [ -n "$WIFI_IF" ]; then
    CURRENT_SSID=$(nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes' | cut -d: -f2)
    if [ "$CURRENT_SSID" = "ESP32-Control" ]; then
        info "断开 WiFi: ESP32-Control ($WIFI_IF)"
        nmcli device disconnect "$WIFI_IF" 2>/dev/null || \
            sudo nmcli device disconnect "$WIFI_IF" 2>/dev/null
        info "WiFi 已断开"
    else
        info "未连接到 ESP32-Control (当前: ${CURRENT_SSID:-无})"
    fi

    # 清理路由
    sudo ip route del 192.168.4.0/24 2>/dev/null && \
        info "已清理 ESP32 路由" || true
else
    info "未检测到无线网卡"
fi

echo ""
info "=== 已停止 ==="
echo ""
echo "  重新连接: ./run.sh"
echo "  烧录固件: ./build_flash.sh"
