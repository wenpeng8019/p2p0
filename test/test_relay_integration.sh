#!/bin/bash

# ==============================================================================
# P2P RELAY 模式完整测试脚本
# ==============================================================================
#
# 功能：
#   - 测试 Relay 模式的所有协议场景（7 个场景）
#   - 验证离线缓存、Trickle ICE、超时机制等新功能
#
# 测试场景：
#   场景 3: 双方在线直接连接（OFFER 立即转发）
#   场景 4: 对端离线候选缓存（Bob 离线 → 缓存 → Bob 上线 → FORWARD）
#   场景 5: 缓存已满（status=2，等待 FORWARD）
#   场景 6: 不支持缓存（空 OFFER，服务器不缓存）
#   场景 7: Trickle ICE（分批发送候选）
#   超时测试: 60 秒未收到 FORWARD 则放弃
#
# 架构：
#   Alice ←→ Relay Server (8888) ←→ Bob
#   - Alice: passive，仅登录等待（--name alice）
#   - Bob: active，主动连接（--name bob --to alice）
#
# 使用方式：
#   ./test/test_relay.sh [scenario]
#     scenario: all | online | offline | cache_full | no_cache | trickle | timeout
#     默认: all（运行所有场景）
#
# ==============================================================================

set -e

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 工作目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$SCRIPT_DIR/relay_logs"
mkdir -p "$LOG_DIR"

# 二进制文件
RELAY_SERVER="$PROJECT_ROOT/build_cmake/p2p_server/p2p_server"
P2P_PING="$PROJECT_ROOT/build_cmake/p2p_ping/p2p_ping"

# Relay 服务器配置
RELAY_PORT=8888
RELAY_HOST="127.0.0.1"

# PID 文件
SERVER_PID=""
ALICE_PID=""
BOB_PID=""

# 测试场景选择
SCENARIO="${1:-all}"

# ==============================================================================
# 辅助函数
# ==============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

log_test() {
    echo ""
    echo -e "${CYAN}========================================${NC}"
    echo -e "${CYAN}$1${NC}"
    echo -e "${CYAN}========================================${NC}"
}

# 清理所有进程
cleanup() {
    log_info "清理测试环境..."
    
    # 杀死所有相关进程
    pkill -9 server 2>/dev/null || true
    pkill -9 p2p_ping 2>/dev/null || true
    
    # 等待端口释放
    sleep 1
    
    log_info "清理完成"
}

# 检查二进制文件
check_binaries() {
    log_info "检查二进制文件..."
    
    if [ ! -f "$RELAY_SERVER" ]; then
        log_error "Relay 服务器未编译: $RELAY_SERVER"
        log_info "请运行: make -C build_cmake"
        exit 1
    fi
    
    if [ ! -f "$P2P_PING" ]; then
        log_error "p2p_ping 未编译: $P2P_PING"
        log_info "请运行: make -C build_cmake"
        exit 1
    fi
    
    log_success "二进制文件检查通过"
}

# 启动 Relay 服务器
start_relay_server() {
    local max_pending="${1:-10}"  # 默认缓存 10 个候选
    
    log_info "启动 Relay 服务器 (端口: $RELAY_PORT, 最大缓存: $max_pending)..."
    
    # 停止旧服务器
    pkill -9 server 2>/dev/null || true
    sleep 1
    
    # 启动新服务器
    local server_log="$LOG_DIR/server_${SCENARIO}.log"
    RELAY_MAX_PENDING_CANDIDATES=$max_pending \
        "$RELAY_SERVER" "$RELAY_PORT" > "$server_log" 2>&1 &
    SERVER_PID=$!
    
    # 等待服务器启动
    sleep 2
    
    # 检查服务器是否在运行
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        log_error "服务器启动失败，查看日志: $server_log"
        cat "$server_log"
        exit 1
    fi
    
    log_success "Relay 服务器启动成功 (PID: $SERVER_PID)"
}

# 启动 Alice (passive)
start_alice() {
    log_info "启动 Alice (passive)..."
    
    # 停止旧进程
    pkill -9 -f "p2p_ping.*alice" 2>/dev/null || true
    sleep 1
    
    # 启动 Alice
    local alice_log="$LOG_DIR/alice_${SCENARIO}.log"
    "$P2P_PING" \
        --server "$RELAY_HOST" \
        --name alice \
        > "$alice_log" 2>&1 &
    ALICE_PID=$!
    
    sleep 2
    
    # 检查进程
    if ! kill -0 $ALICE_PID 2>/dev/null; then
        log_error "Alice 启动失败，查看日志: $alice_log"
        cat "$alice_log"
        exit 1
    fi
    
    log_success "Alice 启动成功 (PID: $ALICE_PID)"
}

# 启动 Bob (active)
start_bob() {
    local delay="${1:-0}"  # 延迟启动（秒）
    
    if [ "$delay" -gt 0 ]; then
        log_info "延迟 $delay 秒后启动 Bob..."
        sleep "$delay"
    fi
    
    log_info "启动 Bob (active, 连接到 alice)..."
    
    # 停止旧进程
    pkill -9 -f "p2p_ping.*bob" 2>/dev/null || true
    sleep 1
    
    # 启动 Bob
    local bob_log="$LOG_DIR/bob_${SCENARIO}.log"
    "$P2P_PING" \
        --server "$RELAY_HOST" \
        --name bob \
        --to alice \
        > "$bob_log" 2>&1 &
    BOB_PID=$!
    
    sleep 2
    
    # 检查进程
    if ! kill -0 $BOB_PID 2>/dev/null; then
        log_error "Bob 启动失败，查看日志: $bob_log"
        cat "$bob_log"
        exit 1
    fi
    
    log_success "Bob 启动成功 (PID: $BOB_PID)"
}

# 验证服务器转发功能（场景3：双方在线）
verify_server_forwarding_online() {
    local timeout="${1:-15}"
    local server_log="$LOG_DIR/server_${SCENARIO}.log"
    
    log_info "验证服务器转发 (最多 $timeout 秒)..."
    
    for i in $(seq 1 $timeout); do
        echo -n "."
        sleep 1
        
        # 检查服务器是否转发了 CONNECT→OFFER 和 ANSWER→FORWARD
        if grep -q "Forwarded .* candidates to online user 'alice'" "$server_log" 2>/dev/null && \
           grep -q "Forwarded .* candidates to online user 'bob'" "$server_log" 2>/dev/null; then
            echo ""
            log_success "服务器转发成功! (用时 $i 秒)"
            log_success "✓ CONNECT → OFFER (bob → alice)"
            log_success "✓ ANSWER → FORWARD (alice → bob)"
            return 0
        fi
    done
    
    echo ""
    log_error "服务器转发超时 ($timeout 秒)"
    return 1
}

# 等待并检查连接（保留用于其他场景）
wait_for_connection() {
    local timeout="${1:-30}"
    local alice_log="$LOG_DIR/alice_${SCENARIO}.log"
    local bob_log="$LOG_DIR/bob_${SCENARIO}.log"
    
    log_info "等待连接建立 (最多 $timeout 秒)..."
    
    for i in $(seq 1 $timeout); do
        echo -n "."
        sleep 1
        
        # 检查双方是否都显示 CONNECTED
        if grep -q "\[CONNECTED\]" "$alice_log" 2>/dev/null && \
           grep -q "\[CONNECTED\]" "$bob_log" 2>/dev/null; then
            echo ""
            log_success "连接建立成功! (用时 $i 秒)"
            return 0
        fi
    done
    
    echo ""
    log_error "连接超时 ($timeout 秒)"
    return 1
}

# 检查日志中的关键字
check_log_keyword() {
    local log_file="$1"
    local keyword="$2"
    local description="$3"
    
    if grep -q "$keyword" "$log_file" 2>/dev/null; then
        log_success "$description: 找到 '$keyword'"
        return 0
    else
        log_warning "$description: 未找到 '$keyword'"
        return 1
    fi
}

# 显示日志摘要
show_log_summary() {
    local alice_log="$LOG_DIR/alice_${SCENARIO}.log"
    local bob_log="$LOG_DIR/bob_${SCENARIO}.log"
    local server_log="$LOG_DIR/server_${SCENARIO}.log"
    
    echo ""
    log_info "========== Alice 日志摘要 =========="
    grep -E "(LOGIN|OFFER|ANSWER|CANDIDATE|FORWARD|CONNECTED)" "$alice_log" 2>/dev/null | tail -20 || true
    
    echo ""
    log_info "========== Bob 日志摘要 =========="
    grep -E "(LOGIN|OFFER|ANSWER|CANDIDATE|FORWARD|CONNECTED)" "$bob_log" 2>/dev/null | tail -20 || true
    
    echo ""
    log_info "========== Server 日志摘要 =========="
    grep -E "(LOGIN|CONNECT|OFFER|ANSWER|CANDIDATE|FORWARD|pending_intent)" "$server_log" 2>/dev/null | tail -30 || true
}

# ==============================================================================
# 测试场景
# ==============================================================================

# 场景 3: 双方在线直接连接
test_scenario_online() {
    log_test "场景 3: 双方在线直接连接"
    
    SCENARIO="online"
    
    # 启动服务器（缓存 10 个候选）
    start_relay_server 10
    
    # 先启动 Alice（等待连接）
    start_alice
    
    # 延迟 3 秒启动 Bob（确保 Alice 已登录）
    start_bob 3
    
    # 验证服务器转发（15 秒超时）
    if verify_server_forwarding_online 15; then
        log_success "✅ 场景 3 测试通过：服务器转发正常"
        
        # 验证日志细节
        local server_log="$LOG_DIR/server_${SCENARIO}.log"
        echo ""
        log_info "========== 验证详情 =========="
        
        # 统计转发次数
        local offer_count=$(grep -c "Forwarded .* candidates to online user 'alice'" "$server_log" 2>/dev/null || echo 0)
        local forward_count=$(grep -c "Forwarded .* candidates to online user 'bob'" "$server_log" 2>/dev/null || echo 0)
        local ack_count=$(grep -c "Sent CONNECT_ACK to bob (status=0" "$server_log" 2>/dev/null || echo 0)
        
        log_success "✓ CONNECT_ACK 数量: $ack_count (status=0, 对端在线)"
        log_success "✓ OFFER 转发次数 (bob→alice): $offer_count"
        log_success "✓ FORWARD 转发次数 (alice→bob): $forward_count"
        
        show_log_summary
        return 0
    else
        log_error "❌ 场景 3 测试失败：服务器转发异常"
        show_log_summary
        return 1
    fi
}

# 场景 4: 对端离线候选缓存
test_scenario_offline_cache() {
    log_test "场景 4: 对端离线候选缓存"
    
    SCENARIO="offline"
    
    # 启动服务器（缓存 10 个候选）
    start_relay_server 10
    
    # 只启动 Bob（Alice 离线）
    log_info "仅启动 Bob（Alice 离线）..."
    start_bob 0
    
    # 等待 5 秒（Bob 发送候选到服务器）
    log_info "等待 5 秒，Bob 发送候选到服务器..."
    sleep 5
    
    # 检查服务器日志中的缓存记录
    if check_log_keyword "$LOG_DIR/server_${SCENARIO}.log" "pending_intent" "服务器记录连接意图"; then
        log_success "服务器成功缓存候选"
    else
        log_warning "服务器未记录连接意图（可能直接转发了）"
    fi
    
    # 启动 Alice（上线）
    log_info "启动 Alice（上线，应该收到缓存的 OFFER）..."
    start_alice
    
    # 等待连接（30 秒超时）
    if wait_for_connection 30; then
        log_success "场景 4 测试通过"
        
        # 验证 Alice 收到 OFFER（来自缓存）
        check_log_keyword "$LOG_DIR/alice_${SCENARIO}.log" "OFFER" "Alice 收到缓存的 OFFER"
        
        # 验证 Bob 收到 FORWARD
        check_log_keyword "$LOG_DIR/bob_${SCENARIO}.log" "FORWARD" "Bob 收到 FORWARD（恢复发送）"
        
        show_log_summary
        return 0
    else
        log_error "场景 4 测试失败"
        show_log_summary
        return 1
    fi
}

# 场景 5: 缓存已满
test_scenario_cache_full() {
    log_test "场景 5: 缓存已满（status=2）"
    
    SCENARIO="cache_full"
    
    # 启动服务器（缓存 2 个候选，很快满）
    start_relay_server 2
    
    # 只启动 Bob（Alice 离线）
    log_info "仅启动 Bob（Alice 离线）..."
    start_bob 0
    
    # 等待 8 秒（Bob 发送多个候选，缓存满）
    log_info "等待 8 秒，Bob 发送多个候选，缓存应该满了..."
    sleep 8
    
    # 检查 Bob 是否进入等待状态
    if check_log_keyword "$LOG_DIR/bob_${SCENARIO}.log" "waiting_for_peer" "Bob 进入等待状态"; then
        log_success "Bob 检测到缓存已满，停止发送"
    else
        log_warning "Bob 未进入等待状态（可能缓存未满）"
    fi
    
    # 启动 Alice（上线，服务器应该发送空 OFFER + FORWARD）
    log_info "启动 Alice（上线，应该收到空 OFFER）..."
    start_alice
    
    # 等待连接（30 秒超时）
    if wait_for_connection 30; then
        log_success "场景 5 测试通过"
        
        # 验证 Alice 收到空 OFFER（candidates 数组为空）
        # 注意：这里需要检查 OFFER 的内容，但 grep 无法做到，仅检查关键词
        check_log_keyword "$LOG_DIR/alice_${SCENARIO}.log" "OFFER" "Alice 收到 OFFER"
        
        # 验证 Bob 收到 FORWARD（恢复发送）
        check_log_keyword "$LOG_DIR/bob_${SCENARIO}.log" "FORWARD" "Bob 收到 FORWARD（恢复发送）"
        
        show_log_summary
        return 0
    else
        log_error "场景 5 测试失败"
        show_log_summary
        return 1
    fi
}

# 场景 6: 不支持缓存
test_scenario_no_cache() {
    log_test "场景 6: 不支持缓存（空 OFFER）"
    
    SCENARIO="no_cache"
    
    # 启动服务器（缓存 0 个候选，不支持缓存）
    start_relay_server 0
    
    # 只启动 Bob（Alice 离线）
    log_info "仅启动 Bob（Alice 离线）..."
    start_bob 0
    
    # 等待 5 秒（Bob 发送候选，但服务器不缓存）
    log_info "等待 5 秒，Bob 发送候选（服务器不缓存）..."
    sleep 5
    
    # 检查服务器日志（应该返回 status=2，不缓存）
    check_log_keyword "$LOG_DIR/server_${SCENARIO}.log" "CONNECT.*status.*2" "服务器返回 status=2"
    
    # 启动 Alice（上线，应该收到空 OFFER）
    log_info "启动 Alice（上线，应该收到空 OFFER）..."
    start_alice
    
    # 等待连接（30 秒超时）
    if wait_for_connection 30; then
        log_success "场景 6 测试通过"
        
        # 验证 Alice 收到空 OFFER
        check_log_keyword "$LOG_DIR/alice_${SCENARIO}.log" "OFFER" "Alice 收到空 OFFER"
        
        # 验证 Bob 收到 FORWARD
        check_log_keyword "$LOG_DIR/bob_${SCENARIO}.log" "FORWARD" "Bob 收到 FORWARD"
        
        show_log_summary
        return 0
    else
        log_error "场景 6 测试失败"
        show_log_summary
        return 1
    fi
}

# 场景 7: Trickle ICE（分批发送）
test_scenario_trickle_ice() {
    log_test "场景 7: Trickle ICE（分批发送候选）"
    
    SCENARIO="trickle"
    
    # 启动服务器（缓存 10 个候选）
    start_relay_server 10
    
    # 先启动 Alice
    start_alice
    
    # 启动 Bob（延迟 3 秒）
    start_bob 3
    
    # 等待 15 秒（观察 Trickle ICE 流程）
    log_info "等待 15 秒，观察 Trickle ICE 流程..."
    sleep 15
    
    # 检查是否有多个 CANDIDATE 消息（Trickle ICE）
    local alice_candidates=$(grep -c "CANDIDATE" "$LOG_DIR/alice_${SCENARIO}.log" 2>/dev/null || echo "0")
    local bob_candidates=$(grep -c "CANDIDATE" "$LOG_DIR/bob_${SCENARIO}.log" 2>/dev/null || echo "0")
    
    log_info "Alice 发送候选数: $alice_candidates"
    log_info "Bob 发送候选数: $bob_candidates"
    
    if [ "$alice_candidates" -gt 1 ] || [ "$bob_candidates" -gt 1 ]; then
        log_success "检测到 Trickle ICE（分批发送候选）"
    else
        log_warning "未检测到 Trickle ICE（候选可能一次发送）"
    fi
    
    # 等待连接
    if wait_for_connection 30; then
        log_success "场景 7 测试通过"
        show_log_summary
        return 0
    else
        log_error "场景 7 测试失败"
        show_log_summary
        return 1
    fi
}

# 超时测试: 60 秒未收到 FORWARD
test_scenario_timeout() {
    log_test "超时测试: 60 秒未收到 FORWARD"
    
    SCENARIO="timeout"
    
    # 启动服务器（缓存 2 个候选，很快满）
    start_relay_server 2
    
    # 只启动 Bob（Alice 离线，永远不上线）
    log_info "仅启动 Bob（Alice 永远不上线）..."
    start_bob 0
    
    # 等待 65 秒（超过 60 秒超时）
    log_info "等待 65 秒，测试超时机制..."
    for i in {1..65}; do
        echo -n "."
        sleep 1
    done
    echo ""
    
    # 检查 Bob 是否超时放弃
    if check_log_keyword "$LOG_DIR/bob_${SCENARIO}.log" "timeout\|Timeout\|TIMEOUT\|放弃" "Bob 超时放弃"; then
        log_success "超时测试通过（Bob 正确检测超时）"
        show_log_summary
        return 0
    else
        log_warning "超时测试未通过（Bob 未检测超时，或日志关键词不匹配）"
        show_log_summary
        return 1
    fi
}

# ==============================================================================
# 主函数
# ==============================================================================

main() {
    echo ""
    log_info "=========================================="
    log_info "P2P Relay 模式完整测试"
    log_info "=========================================="
    echo ""
    
    # 清理旧环境
    cleanup
    
    # 检查二进制文件
    check_binaries
    
    # 测试结果统计
    local total=0
    local passed=0
    local failed=0
    
    # 运行测试场景
    if [ "$SCENARIO" == "all" ] || [ "$SCENARIO" == "online" ]; then
        ((total++))
        if test_scenario_online; then
            ((passed++))
        else
            ((failed++))
        fi
        cleanup
        sleep 2
    fi
    
    if [ "$SCENARIO" == "all" ] || [ "$SCENARIO" == "offline" ]; then
        ((total++))
        if test_scenario_offline_cache; then
            ((passed++))
        else
            ((failed++))
        fi
        cleanup
        sleep 2
    fi
    
    if [ "$SCENARIO" == "all" ] || [ "$SCENARIO" == "cache_full" ]; then
        ((total++))
        if test_scenario_cache_full; then
            ((passed++))
        else
            ((failed++))
        fi
        cleanup
        sleep 2
    fi
    
    if [ "$SCENARIO" == "all" ] || [ "$SCENARIO" == "no_cache" ]; then
        ((total++))
        if test_scenario_no_cache; then
            ((passed++))
        else
            ((failed++))
        fi
        cleanup
        sleep 2
    fi
    
    if [ "$SCENARIO" == "all" ] || [ "$SCENARIO" == "trickle" ]; then
        ((total++))
        if test_scenario_trickle_ice; then
            ((passed++))
        else
            ((failed++))
        fi
        cleanup
        sleep 2
    fi
    
    if [ "$SCENARIO" == "all" ] || [ "$SCENARIO" == "timeout" ]; then
        ((total++))
        if test_scenario_timeout; then
            ((passed++))
        else
            ((failed++))
        fi
        cleanup
        sleep 2
    fi
    
    # 最终清理
    cleanup
    
    # 显示测试结果
    echo ""
    log_info "=========================================="
    log_info "测试结果汇总"
    log_info "=========================================="
    log_info "总计: $total"
    log_success "通过: $passed"
    log_error "失败: $failed"
    echo ""
    
    if [ $failed -eq 0 ]; then
        log_success "所有测试通过！"
        log_info "日志目录: $LOG_DIR"
        exit 0
    else
        log_error "部分测试失败"
        log_info "日志目录: $LOG_DIR"
        exit 1
    fi
}

# 捕获退出信号，清理进程
trap cleanup EXIT INT TERM

# 运行主函数
main
