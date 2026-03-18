#!/bin/bash
# Stress test: entity creation reliability
# Usage: ./stress_test.sh [num_tests]

N=${1:-5}
source /opt/ros/jazzy/setup.bash 2>/dev/null || true

PASS=0
FAIL=0

reset_esp32() {
    python3 << 'EOF' 2>/dev/null
import esptool, sys, os
devnull = open(os.devnull, 'w')
old_out, old_err = sys.stdout, sys.stderr
sys.stdout, sys.stderr = devnull, devnull
try:
    esptool.main(['--port','/dev/ttyUSB0','--after','hard_reset','read_mac'])
finally:
    sys.stdout, sys.stderr = old_out, old_err
    devnull.close()
EOF
}

for i in $(seq 1 "$N"); do
    echo "=== Test $i/$N ==="

    # Stop agent
    docker stop micro-ros-agent >/dev/null 2>&1 || true
    sleep 1

    # Reset ESP32
    reset_esp32
    echo "  ESP32 reset, waiting 15s for WiFi..."
    sleep 15

    # Start agent
    docker run -d --rm --name micro-ros-agent --net=host \
        microros/micro-ros-agent:jazzy udp4 --port 8888 -v4 >/dev/null 2>&1
    echo "  Agent started, waiting 30s for entities..."
    sleep 30

    # Check result
    COUNT=$(docker logs micro-ros-agent 2>&1 | grep -c "create_" || echo 0)
    if [ "$COUNT" -ge 18 ]; then
        PASS=$((PASS+1))
        echo "  PASS ($COUNT entities)"
    else
        FAIL=$((FAIL+1))
        echo "  FAIL ($COUNT entities)"
    fi
done

echo ""
echo "==============================="
echo "  RESULTS: $PASS/$N pass, $FAIL/$N fail"
echo "==============================="
