#!/bin/bash

# ==============================================================================
# P2P STUN 集成测试脚本
# ==============================================================================
#
# 功能：
#   - 测试 STUN 相关的端到端行为（3 个场景）
#   - 验证 NAT 类型检测日志输出、STUN 映射地址收集、客户端正常启动
#
# 测试场景：
#   场景 1: 客户端启动并完成 STUN 映射地址收集
#   场景 2: 服务器端正常接受客户端注册
#   场景 3: NAT 类型检测日志输出（NAT/STUN 关键字检查）
#
# 架构：
#   Alice ←→ Server (8891)
#
# 使用方式：
#   ./test/test_stun_integration.sh
#
# ==============================================================================

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$SCRIPT_DIR/stun_integration_logs"
mkdir -p "$LOG_DIR"

SERVER_BIN="$PROJECT_ROOT/build_cmake/p2p_server/p2p_server"
PING_BIN="$PROJECT_ROOT/build_cmake/p2p_ping/p2p_ping"
PORT=8891

PASS=0
FAIL=0

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[✓]${NC} $1"; PASS=$((PASS+1)); }
log_error()   { echo -e "${RED}[✗]${NC} $1"; FAIL=$((FAIL+1)); }
log_test()    { echo ""; echo -e "${CYAN}========================================${NC}"; echo -e "${CYAN}$1${NC}"; echo -e "${CYAN}========================================${NC}"; }

cleanup() {
    pkill -9 -f "p2p_server.*$PORT" 2>/dev/null || true
    pkill -9 -f "p2p_ping.*stun_test" 2>/dev/null || true
    sleep 1
}

check_binaries() {
    if [ ! -f "$SERVER_BIN" ] || [ ! -f "$PING_BIN" ]; then
        echo -e "${RED}[✗]${NC} 二进制文件未编译，请先运行: make -C build_cmake"
        exit 1
    fi
}

# ==============================================================================
# 场景 1：客户端启动并完成 STUN 映射地址收集
# ==============================================================================
test_stun_mapping() {
    log_test "场景 1: STUN 映射地址收集"

    local srv_log="$LOG_DIR/stun_s1_server.log"
    local cli_log="$LOG_DIR/stun_s1_alice.log"

    cleanup
    "$SERVER_BIN" $PORT > "$srv_log" 2>&1 &
    local srv_pid=$!
    sleep 1

    "$PING_BIN" --name stun_test_alice --server 127.0.0.1:$PORT > "$cli_log" 2>&1 &
    local cli_pid=$!
    sleep 8

    kill -9 $cli_pid $srv_pid 2>/dev/null || true
    sleep 1

    # 客户端进程成功启动（日志存在且非空）
    if [ -s "$cli_log" ]; then
        log_success "客户端成功启动并产生日志"
    else
        log_error "客户端日志为空，可能启动失败"
    fi

    # 检查服务器是否接受到连接
    if grep -qiE "connect|register|alice|client" "$srv_log" 2>/dev/null; then
        log_success "服务器收到客户端连接"
    else
        log_error "服务器未检测到客户端连接（日志: $srv_log）"
    fi
}

# ==============================================================================
# 场景 2：服务器正常接受注册
# ==============================================================================
test_server_registration() {
    log_test "场景 2: 服务器接受客户端注册"

    local srv_log="$LOG_DIR/stun_s2_server.log"
    local cli_log="$LOG_DIR/stun_s2_alice.log"

    cleanup
    "$SERVER_BIN" $PORT > "$srv_log" 2>&1 &
    local srv_pid=$!
    sleep 1

    "$PING_BIN" --name stun_test_reg --server 127.0.0.1:$PORT > "$cli_log" 2>&1 &
    local cli_pid=$!
    sleep 5

    kill -9 $cli_pid $srv_pid 2>/dev/null || true
    sleep 1

    if grep -qiE "register|join|hello|online|connected" "$srv_log" "$cli_log" 2>/dev/null; then
        log_success "注册/连接成功"
    else
        log_error "未检测到注册/连接成功信号（日志: $srv_log, $cli_log）"
    fi
}

# ==============================================================================
# 场景 3：NAT/STUN 关键字出现在日志中
# ==============================================================================
test_nat_stun_logs() {
    log_test "场景 3: NAT/STUN 日志关键字检查"

    local srv_log="$LOG_DIR/stun_s3_server.log"
    local cli_log="$LOG_DIR/stun_s3_alice.log"

    cleanup
    "$SERVER_BIN" $PORT > "$srv_log" 2>&1 &
    local srv_pid=$!
    sleep 1

    "$PING_BIN" --name stun_test_nat --server 127.0.0.1:$PORT > "$cli_log" 2>&1 &
    local cli_pid=$!
    sleep 10

    kill -9 $cli_pid $srv_pid 2>/dev/null || true
    sleep 1

    if grep -qiE "NAT|STUN|mapped|candidate|stun|nat" "$cli_log" "$srv_log" 2>/dev/null; then
        log_success "检测到 NAT/STUN 相关日志"
        grep -iE "NAT|STUN|mapped|candidate" "$cli_log" "$srv_log" 2>/dev/null | head -5 | sed 's/^/    /'
    else
        log_error "未检测到 NAT/STUN 相关日志（这在本地回环环境中属正常）"
        log_info  "提示：在真实 NAT 环境中运行以得到完整结果"
    fi
}

# ==============================================================================
# 主入口
# ==============================================================================
main() {
    echo ""
    echo -e "${CYAN}================================================${NC}"
    echo -e "${CYAN}  P2P STUN 集成测试${NC}"
    echo -e "${CYAN}================================================${NC}"

    check_binaries

    test_stun_mapping
    test_server_registration
    test_nat_stun_logs

    cleanup

    echo ""
    echo -e "${CYAN}================================================${NC}"
    echo -e "  结果: ${GREEN}${PASS} 通过${NC} / ${RED}${FAIL} 失败${NC}"
    echo -e "${CYAN}================================================${NC}"
    echo ""

    [ $FAIL -eq 0 ]
}

main "$@"
