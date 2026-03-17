#!/bin/bash
# ============================================================
# 双网络配置脚本
# 有线网 (eth) -> Internet (默认路由)
# WiFi (wlan) -> ESP32 控制 (仅 192.168.4.0/24)
# ============================================================

set -e
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

ESP32_SSID="ESP32-Control"
ESP32_PASS="esp32ctrl"
ESP32_SUBNET="192.168.4.0/24"
ESP32_IP="192.168.4.1"

# 自动检测网络接口 (选有默认路由的有线网卡)
ETH_IF=$(ip route show default | awk '{print $5}' | head -1)
WIFI_IF=$(ip -o link show | awk -F': ' '{print $2}' | grep -E '^wl' | head -1)

if [ -z "$ETH_IF" ]; then error "未找到有线网卡"; fi
if [ -z "$WIFI_IF" ]; then error "未找到无线网卡"; fi

info "有线网卡: $ETH_IF"
info "无线网卡: $WIFI_IF"

# Step 1: 确保有线网连接正常
ETH_GW=$(ip route show dev "$ETH_IF" | grep default | awk '{print $3}' | head -1)
if [ -z "$ETH_GW" ]; then
    warn "有线网无默认网关，尝试等待..."
    sleep 2
    ETH_GW=$(ip route show dev "$ETH_IF" | grep default | awk '{print $3}' | head -1)
fi
if [ -z "$ETH_GW" ]; then error "有线网无默认网关，请检查网线连接"; fi
info "有线网关: $ETH_GW"

# Step 2: 连接 ESP32 WiFi
info "连接 WiFi: $ESP32_SSID ..."
nmcli device wifi rescan ifname "$WIFI_IF" 2>/dev/null || sudo nmcli device wifi rescan ifname "$WIFI_IF" 2>/dev/null || true
sleep 2

# 检查是否已连接
CURRENT_SSID=$(nmcli -t -f active,ssid dev wifi | grep '^yes' | cut -d: -f2)
if [ "$CURRENT_SSID" = "$ESP32_SSID" ]; then
    info "已连接到 $ESP32_SSID"
else
    sudo nmcli device wifi connect "$ESP32_SSID" password "$ESP32_PASS" ifname "$WIFI_IF" 2>&1 || \
        error "连接 $ESP32_SSID 失败，请确认 ESP32 已启动"
    info "WiFi 已连接"
fi

sleep 2

# Step 3: 修复路由 - 确保默认路由走有线
info "配置路由策略..."

# 删除 WiFi 添加的默认路由 (如果有)
sudo ip route del default dev "$WIFI_IF" 2>/dev/null || true

# 确保有线网是默认路由
sudo ip route replace default via "$ETH_GW" dev "$ETH_IF"

# 添加 ESP32 子网路由走 WiFi
sudo ip route replace "$ESP32_SUBNET" dev "$WIFI_IF"

info "路由配置完成:"
echo ""
echo "  Internet (默认) -> $ETH_IF (网关: $ETH_GW)"
echo "  ESP32 控制       -> $WIFI_IF ($ESP32_SUBNET)"
echo ""

# Step 4: 验证
info "验证连接..."
echo -n "  Internet: "
if ping -c 1 -W 2 -I "$ETH_IF" 8.8.8.8 &>/dev/null; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FAIL${NC}"
    warn "Internet 连接异常，但不影响 ESP32 控制"
fi

echo -n "  ESP32:    "
if ping -c 1 -W 2 "$ESP32_IP" &>/dev/null; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FAIL${NC}"
    warn "ESP32 不可达，请检查 WiFi 连接和 ESP32 固件"
fi

echo ""
info "=== 配置完成 ==="
echo ""
echo "  运行客户端: python3 ~/Documents/esp32/wifi_echo/wifi_client.py"
echo "  断开 WiFi:  nmcli device disconnect $WIFI_IF"
