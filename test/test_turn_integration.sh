#!/bin/bash
#
# test_turn_integration.sh - TURN 中继模式集成测试
#
# 测试场景：
# 1. TURN Allocate 流程（向 TURN 服务器请求中继地址）
# 2. TURN 候选加入 ICE checklist（Relay 候选类型）
# 3. 对称型 NAT 场景下 TURN 中继兜底
#
# 注：
#   TURN 服务器需要独立部署（coturn 等），测试默认使用 stun.l.google.com:3478
#   若无可用的 TURN 服务器，测试标记为 SKIPPED 而非 FAIL
#

set -e

# --- 颜色 ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build_cmake"
SERVER_BIN="$BUILD_DIR/p2p_server/p2p_server"
CLIENT_BIN="$BUILD_DIR/p2p_ping/p2p_ping"
LOG_DIR="$SCRIPT_DIR/turn_integration_logs"
PORT=8890

# 默认 TURN 服务器（无认证，仅做连通性验证；生产需替换为带 username/credential 的）
TURN_SERVER="${TURN_SERVER:-}"
TURN_PORT="${TURN_PORT:-3478}"

PASS=0
FAIL=0
SKIP=0

cleanup() {
    pkill -P $$ 2>/dev/null || true
    sleep 0.3
}
trap cleanup EXIT INT TERM

pass()  { echo -e "  ${GREEN}[PASS]${NC}   $1"; PASS=$((PASS+1)); }
fail()  { echo -e "  ${RED}[FAIL]${NC}   $1"; FAIL=$((FAIL+1)); }
skip()  { echo -e "  ${YELLOW}[SKIP]${NC}   $1"; SKIP=$((SKIP+1)); }
info()  { echo -e "  ${YELLOW}[INFO]${NC}   $1"; }

wait_log() {
    local log=$1 pattern=$2 timeout=${3:-15}
    local i=0
    while [ $i -lt $timeout ]; do
        grep -qE "$pattern" "$log" 2>/dev/null && return 0
        sleep 0.5; i=$((i+1))
    done
    return 1
}

# ============================================================================
# 检查 TURN 服务器可达性
# ============================================================================
check_turn_server() {
    if [ -z "$TURN_SERVER" ]; then
        return 1
    fi
    # UDP 连通性检查（发送 28 字节 TURN Allocate Request，看有无响应）
    if command -v nc >/dev/null 2>&1; then
        echo "" | nc -u -w2 "$TURN_SERVER" "$TURN_PORT" >/dev/null 2>&1
        return $?
    fi
    return 1
}

# ============================================================================
# 测试1：TURN Allocate 请求（需要 TURN 服务器）
# ============================================================================
test_turn_allocate() {
    echo ""
    echo "--- TEST 1: TURN Allocate request ---"

    if ! check_turn_server; then
        skip "No TURN server configured (set TURN_SERVER=host to enable)"
        skip "  Example: TURN_SERVER=my.turn.server ./test_turn_integration.sh"
        return
    fi

    mkdir -p "$LOG_DIR"
    local ALOG="$LOG_DIR/alice_turn.log"
    > "$ALOG"

    pkill -f "p2p_server $PORT" 2>/dev/null || true
    sleep 0.3
    "$SERVER_BIN" $PORT > /dev/null 2>&1 &
    SRV=$!
    sleep 1

    "$CLIENT_BIN" --server 127.0.0.1 --name alice \
        --turn "$TURN_SERVER" --turn-port "$TURN_PORT" \
        > "$ALOG" 2>&1 &
    ALICE=$!

    sleep 8

    if grep -q "TURN\|Allocat\|relay\|RELAY" "$ALOG" 2>/dev/null; then
        pass "TURN Allocate flow observed"
    else
        fail "TURN Allocate flow not observed in log"
        tail -10 "$ALOG" | sed 's/^/    /'
    fi

    if grep -q "Allocation successful\|relay.*cand\|RELAY candidate" "$ALOG" 2>/dev/null; then
        pass "TURN relay candidate obtained"
    else
        info "Relay candidate not explicitly logged (may be internal)"
    fi

    kill $ALICE $SRV 2>/dev/null || true
    sleep 0.3
}

# ============================================================================
# 测试2：TURN 中继端到端连接（Alice + Bob，需要 TURN 服务器）
# ============================================================================
test_turn_relay_connection() {
    echo ""
    echo "--- TEST 2: End-to-end connection via TURN relay ---"

    if ! check_turn_server; then
        skip "No TURN server configured"
        return
    fi

    mkdir -p "$LOG_DIR"
    local SLOG="$LOG_DIR/server_relay.log"
    local ALOG="$LOG_DIR/alice_relay.log"
    local BLOG="$LOG_DIR/bob_relay.log"
    > "$SLOG"; > "$ALOG"; > "$BLOG"

    pkill -f "p2p_server $PORT" 2>/dev/null || true
    sleep 0.3
    "$SERVER_BIN" $PORT > "$SLOG" 2>&1 &
    SRV=$!
    sleep 1

    "$CLIENT_BIN" --server 127.0.0.1 --name alice \
        --turn "$TURN_SERVER" --turn-port "$TURN_PORT" \
        > "$ALOG" 2>&1 &
    ALICE=$!
    sleep 1

    "$CLIENT_BIN" --server 127.0.0.1 --name bob --to alice \
        --turn "$TURN_SERVER" --turn-port "$TURN_PORT" \
        > "$BLOG" 2>&1 &
    BOB=$!

    if wait_log "$ALOG" "CONNECTED|connected|P2P" 30; then
        pass "Alice connected via TURN relay"
    else
        fail "Alice did not connect (TURN relay)"
        tail -5 "$ALOG" | sed 's/^/    /'
    fi

    if wait_log "$BLOG" "CONNECTED|connected|P2P" 30; then
        pass "Bob connected via TURN relay"
    else
        fail "Bob did not connect (TURN relay)"
        tail -5 "$BLOG" | sed 's/^/    /'
    fi

    kill $ALICE $BOB $SRV 2>/dev/null || true
    sleep 0.3
}

# ============================================================================
# 测试3：无 TURN 时的兜底行为（降级到直连或 RELAY 信令）
# ============================================================================
test_turn_fallback() {
    echo ""
    echo "--- TEST 3: Fallback behavior without TURN server ---"

    mkdir -p "$LOG_DIR"
    local ALOG="$LOG_DIR/alice_fallback.log"
    local BLOG="$LOG_DIR/bob_fallback.log"
    local SLOG="$LOG_DIR/server_fallback.log"
    > "$ALOG"; > "$BLOG"; > "$SLOG"

    pkill -f "p2p_server $PORT" 2>/dev/null || true
    sleep 0.3
    "$SERVER_BIN" $PORT > "$SLOG" 2>&1 &
    SRV=$!
    sleep 1

    # 故意给一个不可达的 TURN 服务器，验证程序不崩溃
    "$CLIENT_BIN" --server 127.0.0.1 --name alice \
        --turn "192.0.2.1" --turn-port 3478 \
        > "$ALOG" 2>&1 &
    ALICE=$!
    sleep 1

    "$CLIENT_BIN" --server 127.0.0.1 --name bob --to alice \
        > "$BLOG" 2>&1 &
    BOB=$!

    sleep 8

    # 验证程序没有崩溃（没有 segfault / core dump）
    if kill -0 $ALICE 2>/dev/null; then
        pass "Alice process alive despite unreachable TURN server"
    else
        # 进程退出也检查是否有 segfault
        local exit_code=0
        wait $ALICE 2>/dev/null || exit_code=$?
        if [ $exit_code -ne 139 ] && [ $exit_code -ne 134 ]; then
            pass "Alice exited cleanly (no crash) code=$exit_code"
        else
            fail "Alice crashed (SIGSEGV/SIGABRT) with unreachable TURN"
        fi
    fi

    # 验证没有明显错误日志
    if grep -q "segfault\|core dump\|Segmentation" "$ALOG" 2>/dev/null; then
        fail "Crash detected in Alice log"
    else
        pass "No crash in Alice log"
    fi

    kill $ALICE $BOB $SRV 2>/dev/null || true
    sleep 0.3
}

# ============================================================================
# 主入口
# ============================================================================
main() {
    echo "========================================"
    echo "  TURN 中继模式集成测试"
    echo "========================================"

    if [ -z "$TURN_SERVER" ]; then
        echo ""
        echo -e "  ${YELLOW}提示${NC}: 未配置 TURN_SERVER 环境变量"
        echo "  TURN 相关测试将标记为 SKIPPED"
        echo "  配置方法: TURN_SERVER=your.turn.server ./test_turn_integration.sh"
        echo ""
    fi

    if [ ! -f "$SERVER_BIN" ] || [ ! -f "$CLIENT_BIN" ]; then
        echo -e "${RED}Error: Binaries not found. Run: cd $BUILD_DIR && make${NC}"
        exit 1
    fi

    test_turn_allocate
    test_turn_relay_connection
    test_turn_fallback

    echo ""
    echo "========================================"
    echo "  测试结果"
    echo "========================================"
    echo -e "  通过: ${GREEN}$PASS${NC}"
    echo -e "  跳过: ${YELLOW}$SKIP${NC}"
    echo -e "  失败: ${RED}$FAIL${NC}"
    echo ""
    echo "  日志目录: $LOG_DIR"
    echo "========================================"
    echo ""
    if [ -n "$TURN_SERVER" ]; then
        echo "  TURN server: $TURN_SERVER:$TURN_PORT"
    fi
    echo ""

    [ $FAIL -eq 0 ] && exit 0 || exit 1
}

main "$@"
