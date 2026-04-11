#!/bin/bash
# =============================================================================
# test_all.sh — P2P Zero 全量回归测试脚本
# =============================================================================
#
# 概述：
#   运行 test/ 目录下所有测试用例，包括：
#   - 独立单元测试（无需参数）
#   - 协议测试（需要 p2p_server）
#   - 端到端测试（需要 p2p_ping + p2p_server）
#
# 用法：
#   ./test_all.sh              # 运行所有测试
#   ./test_all.sh --unit       # 仅运行独立单元测试
#   ./test_all.sh --protocol   # 仅运行协议测试
#   ./test_all.sh --e2e        # 仅运行端到端测试
#   ./test_all.sh --verbose    # 显示详细输出
#
# 测试分类：
#
#   ┌─────────────────────────────────────────────────────────────────────┐
#   │ 独立单元测试（无需外部依赖）                                         │
#   ├─────────────────────────────────────────────────────────────────────┤
#   │ test_transport      传输层 ARQ 重传、流控、分片重组                  │
#   │ test_ws             WS client/server 同进程测试                     │
#   │ test_ws_server_integration  WS + p2p_server 集成测试               │
#   └─────────────────────────────────────────────────────────────────────┘
#
#   ┌─────────────────────────────────────────────────────────────────────┐
#   │ COMPACT 协议测试（需要 p2p_server）                                  │
#   ├─────────────────────────────────────────────────────────────────────┤
#   │ test_compact_register   REGISTER/OFFLINE 注册协议                   │
#   │ test_compact_sync       SYNC/SYNC_ACK 候选同步协议                  │
#   │ test_compact_lifecycle  ALIVE/OFFLINE 生命周期协议                  │
#   │ test_compact_rpc        MSG RPC 消息转发机制                        │
#   │ test_compact_relay      中继转发和 NAT 探测                         │
#   └─────────────────────────────────────────────────────────────────────┘
#
#   ┌─────────────────────────────────────────────────────────────────────┐
#   │ RELAY 协议测试（需要 p2p_server --relay）                           │
#   ├─────────────────────────────────────────────────────────────────────┤
#   │ test_relay_register     ONLINE/SYNC0 注册协议                       │
#   │ test_relay_lifecycle    ALIVE/FIN 生命周期协议                      │
#   │ test_relay_data         DATA 中继转发                               │
#   │ test_relay_rpc          MSG RPC 消息转发机制                        │
#   └─────────────────────────────────────────────────────────────────────┘
#
#   ┌─────────────────────────────────────────────────────────────────────┐
#   │ 端到端测试（需要 p2p_ping + p2p_server）                            │
#   ├─────────────────────────────────────────────────────────────────────┤
#   │ test_ping_c_template    COMPACT 模式 ping 模板测试                  │
#   │ test_ping_c_connect     COMPACT 模式双向连接测试                    │
#   │ test_ping_c_msg         COMPACT 模式消息互发测试                    │
#   │ test_ping_c_sync        COMPACT 模式候选同步测试                    │
#   │ test_ping_c_reconnect   COMPACT 模式断线重连测试                    │
#   │ test_ping_r_template    RELAY 模式 ping 模板测试                    │
#   │ test_ping_r_msg         RELAY 模式消息互发测试                      │
#   │ test_ping_r_sync        RELAY 模式候选同步测试                      │
#   │ test_ping_r_reconnect   RELAY 模式断线重连测试                      │
#   └─────────────────────────────────────────────────────────────────────┘
#
# =============================================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
TEST_BIN_DIR="$BUILD_DIR/test"

# 二进制路径
SERVER_PATH="$BUILD_DIR/p2p_server/p2p_server"
PING_PATH="$BUILD_DIR/p2p_ping/p2p_ping"

# 计数器
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

# 选项
RUN_UNIT=true
RUN_PROTOCOL=true
RUN_E2E=true
VERBOSE=false

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --unit)
            RUN_UNIT=true
            RUN_PROTOCOL=false
            RUN_E2E=false
            shift
            ;;
        --protocol)
            RUN_UNIT=false
            RUN_PROTOCOL=true
            RUN_E2E=false
            shift
            ;;
        --e2e)
            RUN_UNIT=false
            RUN_PROTOCOL=false
            RUN_E2E=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            head -60 "$0" | tail -55
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# 运行单个测试
run_test() {
    local name=$1
    shift
    local cmd=("$@")
    
    printf "  %-35s" "$name"
    
    if [[ ! -x "${cmd[0]}" ]]; then
        echo -e " [${YELLOW}SKIP${NC}] (not built)"
        SKIP_COUNT=$((SKIP_COUNT + 1))
        return
    fi
    
    local output
    local exit_code
    
    if $VERBOSE; then
        echo ""
        "${cmd[@]}" 2>&1 | sed 's/^/    /'
        exit_code=${PIPESTATUS[0]}
    else
        output=$("${cmd[@]}" 2>&1) || true
        exit_code=$?
    fi
    
    if [[ $exit_code -eq 0 ]]; then
        echo -e " [${GREEN}PASS${NC}]"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e " [${RED}FAIL${NC}] (exit=$exit_code)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        if ! $VERBOSE && [[ -n "$output" ]]; then
            echo "$output" | tail -10 | sed 's/^/    /'
        fi
    fi
}

# 打印分隔线
print_section() {
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
}

# 检查构建
check_build() {
    if [[ ! -d "$TEST_BIN_DIR" ]]; then
        echo -e "${RED}错误: 测试未构建${NC}"
        echo "请先运行: cd $PROJECT_ROOT && ./build.sh"
        exit 1
    fi
}

# =============================================================================
# 主程序
# =============================================================================

echo ""
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║           P2P Zero 全量回归测试                               ║"
echo "╚═══════════════════════════════════════════════════════════════╝"

check_build

cd "$TEST_BIN_DIR"

# -----------------------------------------------------------------------------
# 独立单元测试
# -----------------------------------------------------------------------------
if $RUN_UNIT; then
    print_section "独立单元测试（无外部依赖）"
    
    run_test "test_transport" ./test_transport
    run_test "test_ws" ./test_ws
    run_test "test_ws_server_integration" ./test_ws_server_integration
fi

# -----------------------------------------------------------------------------
# COMPACT 协议测试
# -----------------------------------------------------------------------------
if $RUN_PROTOCOL; then
    print_section "COMPACT 协议测试（需要 p2p_server）"
    
    if [[ ! -x "$SERVER_PATH" ]]; then
        echo -e "  ${YELLOW}跳过: p2p_server 未构建${NC}"
        SKIP_COUNT=$((SKIP_COUNT + 5))
    else
        run_test "test_compact_register" ./test_compact_register "$SERVER_PATH"
        run_test "test_compact_sync" ./test_compact_sync "$SERVER_PATH"
        run_test "test_compact_lifecycle" ./test_compact_lifecycle "$SERVER_PATH"
        run_test "test_compact_rpc" ./test_compact_rpc "$SERVER_PATH"
        run_test "test_compact_relay" ./test_compact_relay "$SERVER_PATH"
    fi
    
    print_section "RELAY 协议测试（需要 p2p_server --relay）"
    
    if [[ ! -x "$SERVER_PATH" ]]; then
        echo -e "  ${YELLOW}跳过: p2p_server 未构建${NC}"
        SKIP_COUNT=$((SKIP_COUNT + 4))
    else
        run_test "test_relay_register" ./test_relay_register "$SERVER_PATH"
        run_test "test_relay_lifecycle" ./test_relay_lifecycle "$SERVER_PATH"
        run_test "test_relay_data" ./test_relay_data "$SERVER_PATH"
        run_test "test_relay_rpc" ./test_relay_rpc "$SERVER_PATH"
    fi
fi

# -----------------------------------------------------------------------------
# 端到端测试
# -----------------------------------------------------------------------------
if $RUN_E2E; then
    print_section "端到端测试 - COMPACT 模式（需要 p2p_ping + p2p_server）"
    
    if [[ ! -x "$PING_PATH" ]]; then
        echo -e "  ${YELLOW}跳过: p2p_ping 未构建${NC}"
        SKIP_COUNT=$((SKIP_COUNT + 5))
    elif [[ ! -x "$SERVER_PATH" ]]; then
        echo -e "  ${YELLOW}跳过: p2p_server 未构建${NC}"
        SKIP_COUNT=$((SKIP_COUNT + 5))
    else
        run_test "test_ping_c_template" ./test_ping_c_template "$PING_PATH" "$SERVER_PATH"
        run_test "test_ping_c_connect" ./test_ping_c_connect "$PING_PATH" "$SERVER_PATH"
        run_test "test_ping_c_msg" ./test_ping_c_msg "$PING_PATH" "$SERVER_PATH"
        run_test "test_ping_c_sync" ./test_ping_c_sync "$PING_PATH" "$SERVER_PATH"
        run_test "test_ping_c_reconnect" ./test_ping_c_reconnect "$PING_PATH" "$SERVER_PATH"
    fi
    
    print_section "端到端测试 - RELAY 模式（需要 p2p_ping + p2p_server）"
    
    if [[ ! -x "$PING_PATH" ]]; then
        echo -e "  ${YELLOW}跳过: p2p_ping 未构建${NC}"
        SKIP_COUNT=$((SKIP_COUNT + 4))
    elif [[ ! -x "$SERVER_PATH" ]]; then
        echo -e "  ${YELLOW}跳过: p2p_server 未构建${NC}"
        SKIP_COUNT=$((SKIP_COUNT + 4))
    else
        run_test "test_ping_r_template" ./test_ping_r_template "$PING_PATH" "$SERVER_PATH"
        run_test "test_ping_r_msg" ./test_ping_r_msg "$PING_PATH" "$SERVER_PATH"
        run_test "test_ping_r_sync" ./test_ping_r_sync "$PING_PATH" "$SERVER_PATH"
        run_test "test_ping_r_reconnect" ./test_ping_r_reconnect "$PING_PATH" "$SERVER_PATH"
    fi
fi

# -----------------------------------------------------------------------------
# 汇总
# -----------------------------------------------------------------------------
echo ""
echo "═══════════════════════════════════════════════════════════════"
TOTAL=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))
echo -e "  总计: $TOTAL 个测试"
echo -e "  ${GREEN}通过: $PASS_COUNT${NC}"
if [[ $FAIL_COUNT -gt 0 ]]; then
    echo -e "  ${RED}失败: $FAIL_COUNT${NC}"
fi
if [[ $SKIP_COUNT -gt 0 ]]; then
    echo -e "  ${YELLOW}跳过: $SKIP_COUNT${NC}"
fi
echo "═══════════════════════════════════════════════════════════════"

if [[ $FAIL_COUNT -gt 0 ]]; then
    echo ""
    echo -e "${RED}有测试失败！${NC}"
    exit 1
else
    echo ""
    echo -e "${GREEN}所有测试通过！${NC}"
    exit 0
fi
