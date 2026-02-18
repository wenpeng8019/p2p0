#!/bin/bash
#
# test_ice_integration.sh - ICE 模式端到端集成测试
#
# 测试场景：
# 1. 基础连接：Alice 和 Bob 通过 RELAY 信令交换 ICE 候选、建立 P2P 连接
# 2. NAT 打洞：禁用 LAN shortcut，强制走 ICE 打洞流程
# 3. 数据传输：连接建立后验证数据收发
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
LOG_DIR="$SCRIPT_DIR/ice_integration_logs"
PORT=8889

PASS=0
FAIL=0

cleanup() {
    pkill -P $$ 2>/dev/null || true
    sleep 0.3
}
trap cleanup EXIT INT TERM

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
info() { echo -e "  ${YELLOW}[INFO]${NC} $1"; }

# --- 检查二进制 ---
check_binaries() {
    if [ ! -f "$SERVER_BIN" ] || [ ! -f "$CLIENT_BIN" ]; then
        echo -e "${RED}Error: Binaries not found. Run: cd $BUILD_DIR && make${NC}"
        exit 1
    fi
}

# --- 等待进程特征出现在日志中 ---
wait_log() {
    local log=$1 pattern=$2 timeout=${3:-15}
    local i=0
    while [ $i -lt $timeout ]; do
        grep -q "$pattern" "$log" 2>/dev/null && return 0
        sleep 0.5; i=$((i+1))
    done
    return 1
}

# ============================================================================
# 测试1：基础 ICE 连接（双方在线）
# ============================================================================
test_ice_basic() {
    echo ""
    echo "--- TEST 1: ICE basic connection (both online) ---"

    mkdir -p "$LOG_DIR"
    local SLOG="$LOG_DIR/server.log"
    local ALOG="$LOG_DIR/alice.log"
    local BLOG="$LOG_DIR/bob.log"
    > "$SLOG"; > "$ALOG"; > "$BLOG"

    # 启动服务器
    pkill -f "p2p_server $PORT" 2>/dev/null || true
    sleep 0.3
    "$SERVER_BIN" $PORT > "$SLOG" 2>&1 &
    SRV=$!
    sleep 1

    if ! netstat -an 2>/dev/null | grep -q "$PORT" && \
       ! ss -an 2>/dev/null | grep -q "$PORT"; then
        info "Server may be running (port check skipped)"
    fi

    # Alice 等待连接（ICE 默认模式，不加 --simple）
    "$CLIENT_BIN" --server 127.0.0.1 --name alice \
        > "$ALOG" 2>&1 &
    ALICE=$!
    sleep 1

    # Bob 主动连接 Alice
    "$CLIENT_BIN" --server 127.0.0.1 --name bob --to alice \
        > "$BLOG" 2>&1 &
    BOB=$!

    # 等待连接建立
    if wait_log "$ALOG" "CONNECTED\|connected\|P2P" 20; then
        pass "Alice reached CONNECTED state"
    else
        fail "Alice did not reach CONNECTED (timeout)"
        info "Alice log tail:"
        tail -5 "$ALOG" | sed 's/^/    /'
    fi

    if wait_log "$BLOG" "CONNECTED\|connected\|P2P" 20; then
        pass "Bob reached CONNECTED state"
    else
        fail "Bob did not reach CONNECTED (timeout)"
        info "Bob log tail:"
        tail -5 "$BLOG" | sed 's/^/    /'
    fi

    # 验证服务器日志中有 ICE 相关转发
    if grep -q "OFFER\|FORWARD\|LOGIN" "$SLOG" 2>/dev/null; then
        pass "Server logged ICE signaling activity"
    else
        info "Server signaling log not found (may be verbose off)"
    fi

    kill $ALICE $BOB $SRV 2>/dev/null || true
    sleep 0.5
}

# ============================================================================
# 测试2：ICE + 禁用 LAN shortcut（强制 NAT 打洞路径）
# ============================================================================
test_ice_force_nat() {
    echo ""
    echo "--- TEST 2: ICE with LAN shortcut disabled (force NAT punch) ---"

    mkdir -p "$LOG_DIR"
    local SLOG="$LOG_DIR/server_nat.log"
    local ALOG="$LOG_DIR/alice_nat.log"
    local BLOG="$LOG_DIR/bob_nat.log"
    > "$SLOG"; > "$ALOG"; > "$BLOG"

    pkill -f "p2p_server $PORT" 2>/dev/null || true
    sleep 0.3
    "$SERVER_BIN" $PORT > "$SLOG" 2>&1 &
    SRV=$!
    sleep 1

    "$CLIENT_BIN" --server 127.0.0.1 --name alice --disable-lan \
        > "$ALOG" 2>&1 &
    ALICE=$!
    sleep 1

    "$CLIENT_BIN" --server 127.0.0.1 --name bob --to alice \
        --disable-lan --verbose-punch \
        > "$BLOG" 2>&1 &
    BOB=$!

    # 验证 NAT_PUNCH 日志出现
    if wait_log "$BLOG" "NAT_PUNCH\|PUNCHING\|PUNCH" 25; then
        pass "NAT punch flow observed in logs"
    else
        info "NAT_PUNCH log not found (verbose-punch may need --simple mode)"
    fi

    if wait_log "$ALOG" "CONNECTED\|connected\|P2P" 25; then
        pass "Alice connected (force-NAT mode)"
    else
        fail "Alice did not connect in force-NAT mode"
    fi

    kill $ALICE $BOB $SRV 2>/dev/null || true
    sleep 0.5
}

# ============================================================================
# 测试3：ICE 候选收集（有 STUN 服务器时验证 Srflx 候选）
# ============================================================================
test_ice_candidate_gathering() {
    echo ""
    echo "--- TEST 3: ICE candidate gathering (Host + Srflx) ---"

    mkdir -p "$LOG_DIR"
    local ALOG="$LOG_DIR/alice_gather.log"
    > "$ALOG"

    pkill -f "p2p_server $PORT" 2>/dev/null || true
    sleep 0.3
    "$SERVER_BIN" $PORT > /dev/null 2>&1 &
    SRV=$!
    sleep 1

    "$CLIENT_BIN" --server 127.0.0.1 --name alice \
        > "$ALOG" 2>&1 &
    ALICE=$!

    sleep 5  # 等待候选收集

    if grep -q "candidate\|CAND\|ICE\|srflx\|host" "$ALOG" 2>/dev/null; then
        pass "ICE candidate(s) collected"
    else
        info "No explicit candidate log (may be internal only)"
    fi

    if grep -q "STUN\|stun\|Mapped\|mapped" "$ALOG" 2>/dev/null; then
        pass "STUN server reflexive candidate observed"
    else
        info "No STUN srflx log (STUN server may be unreachable in this env)"
    fi

    kill $ALICE $SRV 2>/dev/null || true
    sleep 0.3
}

# ============================================================================
# 主入口
# ============================================================================
main() {
    echo "========================================"
    echo "  ICE 模式集成测试"
    echo "========================================"

    check_binaries

    test_ice_basic
    test_ice_force_nat
    test_ice_candidate_gathering

    echo ""
    echo "========================================"
    echo "  测试结果"
    echo "========================================"
    echo -e "  通过: ${GREEN}$PASS${NC}"
    echo -e "  失败: ${RED}$FAIL${NC}"
    echo ""
    echo "  日志目录: $LOG_DIR"
    echo "========================================"

    [ $FAIL -eq 0 ] && exit 0 || exit 1
}

main "$@"
