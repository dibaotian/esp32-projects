#!/bin/bash
# ============================================================
# WiFi Echo 项目 - 编译并烧录到 ESP32
# 用法: ./build_flash.sh [PORT]
# 默认串口: /dev/ttyUSB0
# ============================================================

set -e

PORT="${1:-/dev/ttyUSB0}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# 检查 ESP-IDF
if [ ! -f "$IDF_PATH/export.sh" ]; then
    error "ESP-IDF 未找到，请确认 IDF_PATH=$IDF_PATH"
fi

# 激活 ESP-IDF 环境
info "激活 ESP-IDF 环境..."
source "$IDF_PATH/export.sh"

cd "$PROJECT_DIR"

# 设置目标 (如果尚未配置)
if [ ! -f build/build.ninja ]; then
    info "首次构建，设置目标为 ESP32..."
    idf.py set-target esp32
fi

# 编译
info "编译中..."
idf.py build

# 烧录
info "烧录到 $PORT ..."
sudo python3 -m esptool --chip esp32 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_size 2MB --flash_freq 40m \
    0x1000 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/wifi_echo.bin

echo ""
info "=== 编译烧录完成 ==="
echo ""
echo "  启动监控: ./monitor.sh"
echo "  连接测试: python3 ../wifi_client.py"
