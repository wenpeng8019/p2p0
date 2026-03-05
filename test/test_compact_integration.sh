#!/bin/bash
# test_compact_integration.sh - COMPACT 模式综合验证（协议单测 + 端到端）

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build_cmake"
SERVER_BIN="$BUILD_DIR/p2p_server/p2p_server"
CLIENT_BIN="$BUILD_DIR/p2p_ping/p2p_ping"
UNIT_COMPACT_PROTOCOL_BIN="$BUILD_DIR/test/test_compact_protocol"
UNIT_COMPACT_SERVER_BIN="$BUILD_DIR/test/test_compact_server"

LOG_DIR="$SCRIPT_DIR/compact_integration_logs"
mkdir -p "$LOG_DIR"

RUN_ID="$(date +%Y%m%d_%H%M%S)"
SERVER_LOG="$LOG_DIR/server_${RUN_ID}.log"
ALICE_LOG="$LOG_DIR/alice_${RUN_ID}.log"
BOB_LOG="$LOG_DIR/bob_${RUN_ID}.log"
UNIT_LOG="$LOG_DIR/unit_${RUN_ID}.log"

PORT=8888
PASS=0
FAIL=0

cleanup() {
	pkill -P $$ 2>/dev/null || true
}
trap cleanup EXIT INT TERM

pass() { echo "[PASS] $1"; PASS=$((PASS+1)); }
fail() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
info() { echo "[INFO] $1"; }

check_bins() {
	local missing=0
	for b in "$SERVER_BIN" "$CLIENT_BIN" "$UNIT_COMPACT_PROTOCOL_BIN" "$UNIT_COMPACT_SERVER_BIN"; do
		if [ ! -x "$b" ]; then
			echo "[ERROR] binary not found: $b"
			missing=1
		fi
	done
	if [ "$missing" -ne 0 ]; then
		echo "[ERROR] Build required. Try: cd $BUILD_DIR && make"
		exit 1
	fi
}

wait_log() {
	local log_file="$1"
	local pattern="$2"
	local timeout_sec="${3:-20}"
	local i=0
	while [ "$i" -lt "$timeout_sec" ]; do
		if grep -Eiq "$pattern" "$log_file" 2>/dev/null; then
			return 0
		fi
		sleep 1
		i=$((i+1))
	done
	return 1
}

run_unit_tests() {
	info "Running compact unit tests"
	: > "$UNIT_LOG"

	if "$UNIT_COMPACT_PROTOCOL_BIN" >> "$UNIT_LOG" 2>&1; then
		pass "test_compact_protocol"
	else
		fail "test_compact_protocol"
	fi

	if "$UNIT_COMPACT_SERVER_BIN" >> "$UNIT_LOG" 2>&1; then
		pass "test_compact_server"
	else
		fail "test_compact_server"
	fi
}

run_e2e() {
	info "Running compact end-to-end scenario"

	: > "$SERVER_LOG"
	: > "$ALICE_LOG"
	: > "$BOB_LOG"

	"$SERVER_BIN" "$PORT" > "$SERVER_LOG" 2>&1 &
	SERVER_PID=$!
	sleep 1

	"$CLIENT_BIN" --server 127.0.0.1:$PORT --compact --name alice --to bob > "$ALICE_LOG" 2>&1 &
	ALICE_PID=$!
	sleep 1

	"$CLIENT_BIN" --server 127.0.0.1:$PORT --compact --name bob --to alice > "$BOB_LOG" 2>&1 &
	BOB_PID=$!

	# 连接建立（兼容不同日志措辞）
	if wait_log "$ALICE_LOG" "connected|connection established|NAT_CONNECTED|P2P connection established" 25; then
		pass "alice connected"
	else
		fail "alice not connected"
	fi

	if wait_log "$BOB_LOG" "connected|connection established|NAT_CONNECTED|P2P connection established" 25; then
		pass "bob connected"
	else
		fail "bob not connected"
	fi

	# 新逻辑守护：不应出现旧的 ICE 超时回退文案
	if grep -q "forcing exit to REGISTERED" "$ALICE_LOG" "$BOB_LOG" 2>/dev/null; then
		fail "found legacy ICE fallback log"
	else
		pass "legacy ICE fallback log not found"
	fi

	# Server 端 compact 活动可见性（注册/转发/应答任意其一）
	if grep -Eiq "REGISTER|PEER_INFO|MSG_REQ|MSG_RESP|ALIVE" "$SERVER_LOG"; then
		pass "server compact activity observed"
	else
		fail "server compact activity not observed"
	fi

	kill "$SERVER_PID" "$ALICE_PID" "$BOB_PID" 2>/dev/null || true
}

main() {
	check_bins
	run_unit_tests
	run_e2e

	echo ""
	echo "=== COMPACT TEST SUMMARY ==="
	echo "PASS: $PASS"
	echo "FAIL: $FAIL"
	echo "Logs:"
	echo "  $UNIT_LOG"
	echo "  $SERVER_LOG"
	echo "  $ALICE_LOG"
	echo "  $BOB_LOG"

	if [ "$FAIL" -ne 0 ]; then
		exit 1
	fi
}

main "$@"
