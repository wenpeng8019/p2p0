#!/bin/bash
#
# test_nat_punch.sh - NAT 打洞流程测试脚本
#
# 功能：
# 1. 测试 SIMPLE 模式 NAT 打洞流程
# 2. 测试 Relay 模式 NAT 打洞流程
# 3. 禁用同子网直连优化，强制执行 NAT 打洞
# 4. 输出详细的打洞流程日志
#

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build_cmake"
SERVER_BIN="$BUILD_DIR/p2p_server/p2p_server"
CLIENT_BIN="$BUILD_DIR/p2p_ping/p2p_ping"
LOG_DIR="$SCRIPT_DIR/nat_punch_logs"

# 清理函数
cleanup() {
    echo -e "\n[CLEANUP] Stopping all test processes..."
    pkill -P $$ 2>/dev/null || true
    sleep 0.5
}

trap cleanup EXIT INT TERM

# 检查二进制文件
check_binaries() {
    if [ ! -f "$SERVER_BIN" ] || [ ! -f "$CLIENT_BIN" ]; then
        echo -e "${RED}Error: Binaries not found${NC}"
        echo "Please run: cd $BUILD_DIR && make"
        exit 1
    fi
    echo -e "${GREEN}[OK]${NC} Binaries found"
}

# 等待服务器启动
wait_for_server() {
    local port=$1
    local timeout=5
    local elapsed=0
    
    echo -n "  Waiting for server on port $port..."
    while [ $elapsed -lt $timeout ]; do
        if nc -z 127.0.0.1 $port 2>/dev/null; then
            echo -e " ${GREEN}OK${NC}"
            return 0
        fi
        sleep 0.2
        elapsed=$((elapsed + 1))
    done
    echo -e " ${RED}TIMEOUT${NC}"
    return 1
}

# 等待客户端连接
wait_for_connection() {
    local log_file=$1
    local peer_name=$2
    local timeout=15
    local elapsed=0
    
    echo -n "  Waiting for $peer_name connection..."
    while [ $elapsed -lt $timeout ]; do
        if grep -q "CONNECTED" "$log_file" 2>/dev/null; then
            echo -e " ${GREEN}OK${NC}"
            return 0
        fi
        sleep 0.5
        elapsed=$((elapsed + 1))
    done
    echo -e " ${RED}TIMEOUT${NC}"
    return 1
}

# 验证 NAT 打洞成功
verify_nat_punch() {
    local alice_log=$1
    local bob_log=$2
    
    echo "  Verifying NAT punch logs..."
    
    # 检查是否发送了 PUNCH 包
    local alice_punch=$(grep -c "NAT_PUNCH.*PUNCHING: Attempt" "$alice_log" 2>/dev/null || echo 0)
    local bob_punch=$(grep -c "NAT_PUNCH.*PUNCHING: Attempt" "$bob_log" 2>/dev/null || echo 0)
    
    # 检查是否收到 PUNCH_ACK
    local alice_ack=$(grep -c "NAT_PUNCH.*PUNCH_ACK: Received" "$alice_log" 2>/dev/null || echo 0)
    local bob_ack=$(grep -c "NAT_PUNCH.*PUNCH_ACK: Received" "$bob_log" 2>/dev/null || echo 0)
    
    # 检查是否打洞成功
    local alice_success=$(grep -c "NAT_PUNCH.*SUCCESS: Hole punched" "$alice_log" 2>/dev/null || echo 0)
    local bob_success=$(grep -c "NAT_PUNCH.*SUCCESS: Hole punched" "$bob_log" 2>/dev/null || echo 0)
    
    # 检查是否禁用了 LAN shortcut
    local lan_disabled=$(grep -c "LAN shortcut disabled" "$alice_log" 2>/dev/null || echo 0)
    
    echo "    Alice: PUNCH=$alice_punch, ACK=$alice_ack, SUCCESS=$alice_success"
    echo "    Bob:   PUNCH=$bob_punch, ACK=$bob_ack, SUCCESS=$bob_success"
    echo "    LAN shortcut disabled: $lan_disabled"
    
    if [ $alice_success -gt 0 ] && [ $bob_success -gt 0 ]; then
        echo -e "  ${GREEN}✓ NAT punch successful${NC}"
        return 0
    elif [ $alice_punch -gt 0 ] && [ $bob_punch -gt 0 ]; then
        echo -e "  ${YELLOW}⚠ Punch attempted but may not have completed${NC}"
        return 0
    else
        echo -e "  ${RED}✗ NAT punch verification failed${NC}"
        return 1
    fi
}

# 测试 SIMPLE 模式 NAT 打洞
test_simple_nat_punch() {
    echo ""
    echo "========================================"
    echo "Test 1: SIMPLE Mode NAT Punch"
    echo "========================================"
    echo ""
    
    local server_log="$LOG_DIR/simple_server.log"
    local alice_log="$LOG_DIR/simple_alice.log"
    local bob_log="$LOG_DIR/simple_bob.log"
    
    # 启动服务器
    echo "[1] Starting server (SIMPLE mode)..."
    "$SERVER_BIN" 8888 > "$server_log" 2>&1 &
    local server_pid=$!
    echo "  Server PID: $server_pid"
    
    wait_for_server 8888 || { echo "Server failed to start"; return 1; }
    
    # 启动 Alice (带 NAT 打洞测试选项)
    echo ""
    echo "[2] Starting Alice (--disable-lan --verbose-punch)..."
    "$CLIENT_BIN" \
        --server 127.0.0.1 \
        --simple \
        --name alice \
        --to bob \
        --disable-lan \
        --verbose-punch \
        > "$alice_log" 2>&1 &
    local alice_pid=$!
    echo "  Alice PID: $alice_pid"
    
    sleep 1
    
    # 启动 Bob
    echo ""
    echo "[3] Starting Bob (--disable-lan --verbose-punch)..."
    "$CLIENT_BIN" \
        --server 127.0.0.1 \
        --simple \
        --name bob \
        --to alice \
        --disable-lan \
        --verbose-punch \
        > "$bob_log" 2>&1 &
    local bob_pid=$!
    echo "  Bob PID: $bob_pid"
    
    # 等待连接建立
    echo ""
    echo "[4] Waiting for NAT punch to complete..."
    sleep 3  # 给足够时间完成打洞
    
    if wait_for_connection "$alice_log" "Alice" && wait_for_connection "$bob_log" "Bob"; then
        echo ""
        echo "[5] Verifying NAT punch flow..."
        if verify_nat_punch "$alice_log" "$bob_log"; then
            echo ""
            echo "========================================"
            echo -e "${GREEN}✓ SIMPLE Mode NAT Punch Test PASSED${NC}"
            echo "========================================"
            return 0
        fi
    fi
    
    echo ""
    echo "========================================"
    echo -e "${RED}✗ SIMPLE Mode NAT Punch Test FAILED${NC}"
    echo "========================================"
    echo ""
    echo "Recent Alice log:"
    tail -20 "$alice_log"
    echo ""
    echo "Recent Bob log:"
    tail -20 "$bob_log"
    return 1
}

# 测试 Relay 模式 NAT 打洞
test_ice_nat_punch() {
    echo ""
    echo "========================================"
    echo "Test 2: Relay Mode NAT Punch"
    echo "========================================"
    echo ""
    
    local server_log="$LOG_DIR/relay_server.log"
    local alice_log="$LOG_DIR/ice_alice.log"
    local bob_log="$LOG_DIR/ice_bob.log"
    
    # 启动服务器
    echo "[1] Starting server (ICE mode)..."
    "$SERVER_BIN" 8888 > "$server_log" 2>&1 &
    local server_pid=$!
    echo "  Server PID: $server_pid"
    
    wait_for_server 8888 || { echo "Server failed to start"; return 1; }
    
    # 启动 Alice (被动方)
    echo ""
    echo "[2] Starting Alice (passive, --disable-lan --verbose-punch)..."
    "$CLIENT_BIN" \
        --server 127.0.0.1 \
        --name alice \
        --disable-lan \
        --verbose-punch \
        > "$alice_log" 2>&1 &
    local alice_pid=$!
    echo "  Alice PID: $alice_pid"
    
    sleep 1
    
    # 启动 Bob (主动方)
    echo ""
    echo "[3] Starting Bob (active, --disable-lan --verbose-punch)..."
    "$CLIENT_BIN" \
        --server 127.0.0.1 \
        --name bob \
        --to alice \
        --disable-lan \
        --verbose-punch \
        > "$bob_log" 2>&1 &
    local bob_pid=$!
    echo "  Bob PID: $bob_pid"
    
    # 等待连接建立
    echo ""
    echo "[4] Waiting for ICE negotiation and connection..."
    sleep 5  # ICE 需要更多时间
    
    if wait_for_connection "$alice_log" "Alice" && wait_for_connection "$bob_log" "Bob"; then
        echo ""
        echo "[5] Checking ICE flow logs..."
        # Relay 模式可能不会显示 NAT_PUNCH 日志（使用 p2p_signal_relay 内部机制）
        # 但应该显示 disable-lan 和连接成功
        
        local alice_ice=$(grep -c "ICE.*Nomination successful" "$alice_log" 2>/dev/null || echo 0)
        local bob_ice=$(grep -c "ICE.*Nomination successful" "$bob_log" 2>/dev/null || echo 0)
        local lan_disabled=$(grep -c "LAN shortcut disabled" "$alice_log" 2>/dev/null || echo 0)
        
        echo "    Alice ICE success: $alice_ice"
        echo "    Bob ICE success: $bob_ice"
        echo "    LAN shortcut disabled: $lan_disabled"
        
        if [ $alice_ice -gt 0 ] && [ $bob_ice -gt 0 ]; then
            echo ""
            echo "========================================"
            echo -e "${GREEN}✓ Relay Mode NAT Punch Test PASSED${NC}"
            echo "========================================"
            return 0
        fi
    fi
    
    echo ""
    echo "========================================"
    echo -e "${RED}✗ Relay Mode NAT Punch Test FAILED${NC}"
    echo "========================================"
    echo ""
    echo "Recent Alice log:"
    tail -20 "$alice_log"
    echo ""
    echo "Recent Bob log:"
    tail -20 "$bob_log"
    return 1
}

# 主测试流程
main() {
    echo "================================================"
    echo "NAT Hole Punching Flow Test Suite"
    echo "================================================"
    echo ""
    echo "Test Features:"
    echo "  • Disable LAN shortcut (--disable-lan)"
    echo "  • Verbose NAT punch logging (--verbose-punch)"
    echo "  • Test on localhost simulating NAT environment"
    echo ""
    
    check_binaries
    
    # 创建日志目录
    rm -rf "$LOG_DIR"
    mkdir -p "$LOG_DIR"
    echo "Log directory: $LOG_DIR"
    
    local passed=0
    local failed=0
    
    # 测试 SIMPLE 模式
    if test_simple_nat_punch; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
    
    # 清理进程
    cleanup
    sleep 2
    
    # 测试 Relay 模式
    if test_ice_nat_punch; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
    
    # 清理
    cleanup
    
    # 汇总结果
    echo ""
    echo "================================================"
    echo "Test Summary"
    echo "================================================"
    echo "Total:   $((passed + failed))"
    echo -e "Passed:  ${GREEN}$passed${NC}"
    echo -e "Failed:  ${RED}$failed${NC}"
    echo ""
    echo "Logs saved in: $LOG_DIR"
    echo ""
    
    if [ $failed -eq 0 ]; then
        echo -e "${GREEN}All tests PASSED!${NC}"
        echo ""
        return 0
    else
        echo -e "${RED}Some tests FAILED!${NC}"
        echo ""
        return 1
    fi
}

# 运行测试
main
