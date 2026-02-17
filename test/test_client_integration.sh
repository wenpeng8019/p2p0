#!/bin/bash
#
# test_client_integration.sh - P2P 客户端集成测试
#
# 测试策略：
# 1. 启动真实服务器（ICE 或 COMPACT 模式）
# 2. 启动两个 p2p_ping 客户端（alice 和 bob）
# 3. 验证连接建立和数据交换
# 4. 清理环境

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 路径配置
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build_cmake"
SERVER_BIN="${BUILD_DIR}/p2p_server/p2p_server"
CLIENT_BIN="${BUILD_DIR}/p2p_ping/p2p_ping"

# 日志目录
LOG_DIR="${SCRIPT_DIR}/integration_logs"
mkdir -p "${LOG_DIR}"

# 清理函数
cleanup() {
    echo -e "\n${YELLOW}[CLEANUP] Stopping all processes...${NC}"
    
    # 杀死所有子进程
    if [ -n "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null || true
        echo "  - Server (PID $SERVER_PID) stopped"
    fi
    
    if [ -n "$ALICE_PID" ]; then
        kill $ALICE_PID 2>/dev/null || true
        echo "  - Alice (PID $ALICE_PID) stopped"
    fi
    
    if [ -n "$BOB_PID" ]; then
        kill $BOB_PID 2>/dev/null || true
        echo "  - Bob (PID $BOB_PID) stopped"
    fi
    
    # 等待端口释放
    sleep 1
}

# 退出时自动清理
trap cleanup EXIT INT TERM

# 验证二进制文件存在
check_binaries() {
    if [ ! -x "$SERVER_BIN" ]; then
        echo -e "${RED}[ERROR] Server binary not found: $SERVER_BIN${NC}"
        echo "Please build the project first: cd build_cmake && make"
        exit 1
    fi
    
    if [ ! -x "$CLIENT_BIN" ]; then
        echo -e "${RED}[ERROR] Client binary not found: $CLIENT_BIN${NC}"
        echo "Please build the project first: cd build_cmake && make"
        exit 1
    fi
    
    echo -e "${GREEN}[OK] Binaries found${NC}"
}

# 等待服务器启动
wait_for_server() {
    local port=$1
    local mode=$2
    local max_wait=10
    local waited=0
    
    echo -n "  Waiting for $mode server on port $port..."
    
    while [ $waited -lt $max_wait ]; do
        if lsof -i :$port >/dev/null 2>&1; then
            echo -e " ${GREEN}OK${NC}"
            return 0
        fi
        sleep 0.5
        waited=$((waited + 1))
    done
    
    echo -e " ${RED}TIMEOUT${NC}"
    return 1
}

# 等待客户端连接
wait_for_connection() {
    local log_file=$1
    local client_name=$2
    local max_wait=20
    local waited=0
    
    echo -n "  Waiting for $client_name connection..."
    
    while [ $waited -lt $max_wait ]; do
        if grep -q "Connection established" "$log_file" 2>/dev/null; then
            echo -e " ${GREEN}OK${NC}"
            return 0
        fi
        if grep -q "CONNECTED" "$log_file" 2>/dev/null; then
            echo -e " ${GREEN}OK${NC}"
            return 0
        fi
        sleep 0.5
        waited=$((waited + 1))
    done
    
    echo -e " ${RED}TIMEOUT${NC}"
    echo "  Log file: $log_file"
    return 1
}

# 验证数据交换
verify_data_exchange() {
    local alice_log=$1
    local bob_log=$2
    
    echo -n "  Verifying data exchange..."
    
    # 等待数据传输
    sleep 2
    
    # 检查 Alice 发送和接收
    local alice_sent=$(grep -c "Sent PING" "$alice_log" 2>/dev/null || echo 0)
    local alice_recv=$(grep -c "Received:" "$alice_log" 2>/dev/null || echo 0)
    
    # 检查 Bob 发送和接收
    local bob_sent=$(grep -c "Sent PING" "$bob_log" 2>/dev/null || echo 0)
    local bob_recv=$(grep -c "Received:" "$bob_log" 2>/dev/null || echo 0)
    
    if [ $alice_sent -gt 0 ] && [ $bob_recv -gt 0 ]; then
        echo -e " ${GREEN}OK${NC}"
        echo "    Alice sent: $alice_sent, Bob received: $bob_recv"
        return 0
    else
        echo -e " ${RED}FAILED${NC}"
        echo "    Alice sent: $alice_sent, Bob received: $bob_recv"
        return 1
    fi
}

# ============================================================================
# 测试 1: Relay 模式
# ============================================================================
test_ice_mode() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Test 1: Relay Mode Integration${NC}"
    echo -e "${BLUE}========================================${NC}"
    
    local server_log="${LOG_DIR}/relay_server.log"
    local alice_log="${LOG_DIR}/ice_alice.log"
    local bob_log="${LOG_DIR}/ice_bob.log"
    
    # 清理旧日志
    rm -f "$server_log" "$alice_log" "$bob_log"
    
    # 步骤 1: 启动 ICE 服务器
    echo -e "\n${YELLOW}[1] Starting server (supports both TCP/ICE and UDP/SIMPLE)...${NC}"
    "$SERVER_BIN" 8888 > "$server_log" 2>&1 &
    SERVER_PID=$!
    echo "  Server PID: $SERVER_PID"
    
    if ! wait_for_server 8888 "ICE"; then
        echo -e "${RED}[FAIL] Server failed to start${NC}"
        cat "$server_log"
        return 1
    fi
    
    # 步骤 2: 启动 Alice (被动等待)
    echo -e "\n${YELLOW}[2] Starting Alice (passive, waiting for connection)...${NC}"
    "$CLIENT_BIN" --server 127.0.0.1 --name alice > "$alice_log" 2>&1 &
    ALICE_PID=$!
    echo "  Alice PID: $ALICE_PID"
    sleep 2  # 等待 Alice 注册到服务器
    
    # 步骤 3: 启动 Bob (主动连接 Alice)
    echo -e "\n${YELLOW}[3] Starting Bob (active, connecting to alice)...${NC}"
    "$CLIENT_BIN" --server 127.0.0.1 --name bob --to alice > "$bob_log" 2>&1 &
    BOB_PID=$!
    echo "  Bob PID: $BOB_PID"
    
    # 步骤 4: 等待连接建立
    echo -e "\n${YELLOW}[4] Waiting for P2P connection establishment...${NC}"
    
    if ! wait_for_connection "$alice_log" "Alice"; then
        echo -e "${RED}[FAIL] Alice connection timeout${NC}"
        echo "Alice log:"
        tail -20 "$alice_log"
        echo -e "\nBob log:"
        tail -20 "$bob_log"
        return 1
    fi
    
    if ! wait_for_connection "$bob_log" "Bob"; then
        echo -e "${RED}[FAIL] Bob connection timeout${NC}"
        echo "Bob log:"
        tail -20 "$bob_log"
        return 1
    fi
    
    # 步骤 5: 验证数据交换
    echo -e "\n${YELLOW}[5] Verifying data exchange...${NC}"
    
    if ! verify_data_exchange "$alice_log" "$bob_log"; then
        echo -e "${RED}[FAIL] Data exchange verification failed${NC}"
        echo -e "\nAlice log (last 30 lines):"
        tail -30 "$alice_log"
        echo -e "\nBob log (last 30 lines):"
        tail -30 "$bob_log"
        return 1
    fi
    
    # 测试通过
    echo -e "\n${GREEN}========================================${NC}"
    echo -e "${GREEN}✓ Relay Mode Test PASSED${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    # 清理此次测试进程
    kill $ALICE_PID $BOB_PID $SERVER_PID 2>/dev/null || true
    ALICE_PID=""
    BOB_PID=""
    SERVER_PID=""
    sleep 2
    
    return 0
}

# ============================================================================
# 测试 2: COMPACT 模式
# ============================================================================
test_compact_mode() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Test 2: COMPACT Mode Integration${NC}"
    echo -e "${BLUE}========================================${NC}"
    
    local server_log="${LOG_DIR}/compact_server.log"
    local alice_log="${LOG_DIR}/simple_alice.log"
    local bob_log="${LOG_DIR}/simple_bob.log"
    
    # 清理旧日志
    rm -f "$server_log" "$alice_log" "$bob_log"
    
    # 步骤 1: 启动 SIMPLE 服务器
    echo -e "\n${YELLOW}[1] Starting server (supports both TCP/ICE and UDP/SIMPLE)...${NC}"
    "$SERVER_BIN" 8888 > "$server_log" 2>&1 &
    SERVER_PID=$!
    echo "  Server PID: $SERVER_PID"
    
    if ! wait_for_server 8888 "SIMPLE"; then
        echo -e "${RED}[FAIL] Server failed to start${NC}"
        cat "$server_log"
        return 1
    fi
    
    # 步骤 2: 启动 Alice (COMPACT 模式需要指定对端)
    echo -e "\n${YELLOW}[2] Starting Alice (COMPACT mode, alice <-> bob)...${NC}"
    "$CLIENT_BIN" --server 127.0.0.1 --compact --name alice --to bob > "$alice_log" 2>&1 &
    ALICE_PID=$!
    echo "  Alice PID: $ALICE_PID"
    sleep 2  # 等待 Alice 注册到服务器
    
    # 步骤 3: 启动 Bob (主动连接 Alice)
    echo -e "\n${YELLOW}[3] Starting Bob (COMPACT mode, bob <-> alice)...${NC}"
    "$CLIENT_BIN" --server 127.0.0.1 --compact --name bob --to alice > "$bob_log" 2>&1 &
    BOB_PID=$!
    echo "  Bob PID: $BOB_PID"
    
    # 步骤 4: 等待连接建立
    echo -e "\n${YELLOW}[4] Waiting for P2P connection establishment...${NC}"
    
    if ! wait_for_connection "$alice_log" "Alice"; then
        echo -e "${RED}[FAIL] Alice connection timeout${NC}"
        echo "Alice log:"
        tail -20 "$alice_log"
        echo -e "\nBob log:"
        tail -20 "$bob_log"
        return 1
    fi
    
    if ! wait_for_connection "$bob_log" "Bob"; then
        echo -e "${RED}[FAIL] Bob connection timeout${NC}"
        echo "Bob log:"
        tail -20 "$bob_log"
        return 1
    fi
    
    # 步骤 5: 验证数据交换
    echo -e "\n${YELLOW}[5] Verifying data exchange...${NC}"
    
    if ! verify_data_exchange "$alice_log" "$bob_log"; then
        echo -e "${RED}[FAIL] Data exchange verification failed${NC}"
        echo -e "\nAlice log (last 30 lines):"
        tail -30 "$alice_log"
        echo -e "\nBob log (last 30 lines):"
        tail -30 "$bob_log"
        return 1
    fi
    
    # 测试通过
    echo -e "\n${GREEN}========================================${NC}"
    echo -e "${GREEN}✓ COMPACT Mode Test PASSED${NC}"
    echo -e "${GREEN}========================================${NC}"
    
    # 清理此次测试进程
    kill $ALICE_PID $BOB_PID $SERVER_PID 2>/dev/null || true
    ALICE_PID=""
    BOB_PID=""
    SERVER_PID=""
    sleep 2
    
    return 0
}

# ============================================================================
# 主测试流程
# ============================================================================
main() {
    echo -e "${BLUE}================================================${NC}"
    echo -e "${BLUE}P2P Client Integration Test Suite${NC}"
    echo -e "${BLUE}================================================${NC}"
    
    # 检查二进制文件
    check_binaries
    
    # 测试统计
    local total_tests=0
    local passed_tests=0
    local failed_tests=0
    local skipped_tests=0
    
    # 运行 Relay 模式测试
    total_tests=$((total_tests + 1))
    if test_ice_mode; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi
    
    # 运行 COMPACT 模式测试
    total_tests=$((total_tests + 1))
    if test_compact_mode; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi
    
    # 最终统计
    echo ""
    echo -e "${BLUE}================================================${NC}"
    echo -e "${BLUE}Test Summary${NC}"
    echo -e "${BLUE}================================================${NC}"
    echo -e "Total:   $total_tests"
    echo -e "${GREEN}Passed:  $passed_tests${NC}"
    echo -e "${RED}Failed:  $failed_tests${NC}"
    echo -e "${YELLOW}Skipped: $skipped_tests${NC}"
    echo ""
    echo -e "Logs saved in: ${LOG_DIR}"
    echo ""
    
    if [ $failed_tests -gt 0 ]; then
        echo -e "${RED}Some tests FAILED!${NC}"
        return 1
    else
        echo -e "${GREEN}All tests PASSED!${NC}"
        return 0
    fi
}

# 运行主测试
main
