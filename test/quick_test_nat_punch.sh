#!/bin/bash
#
# quick_test_nat_punch.sh - 快速 NAT 打洞测试（带详细日志）
#
# 用法：
#   ./quick_test_nat_punch.sh simple   # 测试 SIMPLE 模式
#   ./quick_test_nat_punch.sh ice      # 测试 Relay 模式
#

MODE="${1:-simple}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build_cmake"
SERVER_BIN="$BUILD_DIR/p2p_server/p2p_server"
CLIENT_BIN="$BUILD_DIR/p2p_ping/p2p_ping"

cleanup() {
    echo ""
    echo "Cleaning up..."
    pkill -P $$ 2>/dev/null || true
    sleep 0.5
}

trap cleanup EXIT INT TERM

case "$MODE" in
    simple)
        echo "========================================="
        echo "NAT Punch Test - SIMPLE Mode"
        echo "========================================="
        echo "Features:"
        echo "  • --disable-lan: Force NAT punch"
        echo "  • --verbose-punch: Show detailed logs"
        echo ""
        
        # 启动服务器
        echo "Starting server on port 8888..."
        "$SERVER_BIN" 8888 &
        sleep 1
        
        # 启动 Alice (新终端)
        echo ""
        echo "Starting Alice in new terminal..."
        osascript -e 'tell application "Terminal"
            do script "cd '"$PROJECT_ROOT"' && '"$CLIENT_BIN"' --server 127.0.0.1 --simple --name alice --to bob --disable-lan --verbose-punch"
        end tell' &
        
        sleep 1
        
        # 启动 Bob (新终端)
        echo "Starting Bob in new terminal..."
        osascript -e 'tell application "Terminal"
            do script "cd '"$PROJECT_ROOT"' && '"$CLIENT_BIN"' --server 127.0.0.1 --simple --name bob --to alice --disable-lan --verbose-punch"
        end tell' &
        
        echo ""
        echo "✓ Test started! Check the terminal windows for logs."
        echo "  Look for [NAT_PUNCH] markers to see the punch flow"
        echo ""
        echo "Press Ctrl+C to stop the test"
        wait
        ;;
        
    ice)
        echo "========================================="
        echo "NAT Punch Test - Relay Mode"
        echo "========================================="
        echo "Features:"
        echo "  • --disable-lan: Force NAT punch"
        echo "  • --verbose-punch: Show detailed logs"
        echo ""
        
        # 启动服务器
        echo "Starting server on port 8888..."
        "$SERVER_BIN" 8888 &
        sleep 1
        
        # 启动 Alice (被动方，新终端)
        echo ""
        echo "Starting Alice (passive) in new terminal..."
        osascript -e 'tell application "Terminal"
            do script "cd '"$PROJECT_ROOT"' && '"$CLIENT_BIN"' --server 127.0.0.1 --name alice --disable-lan --verbose-punch"
        end tell' &
        
        sleep 1
        
        # 启动 Bob (主动方，新终端)
        echo "Starting Bob (active) in new terminal..."
        osascript -e 'tell application "Terminal"
            do script "cd '"$PROJECT_ROOT"' && '"$CLIENT_BIN"' --server 127.0.0.1 --name bob --to alice --disable-lan --verbose-punch"
        end tell' &
        
        echo ""
        echo "✓ Test started! Check the terminal windows for logs."
        echo "  Look for [NAT_PUNCH] and [ICE] markers"
        echo ""
        echo "Press Ctrl+C to stop the test"
        wait
        ;;
        
    *)
        echo "Usage: $0 [simple|ice]"
        echo ""
        echo "Examples:"
        echo "  $0 simple   # Test SIMPLE mode NAT punch"
        echo "  $0 ice      # Test ICE mode NAT punch"
        exit 1
        ;;
esac
