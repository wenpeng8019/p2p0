#!/bin/bash

# ==============================================================================
# P2P PUBSUB (GitHub Gist) 模式连通性测试
# ==============================================================================
# 
# 功能：
#   - 测试使用 GitHub Gist 作为信令通道的 P2P 连接
#   - 验证 PUB/SUB 角色协商和信令交换
#   - 检查 ICE 候选者收集和 NAT 穿透
#
# 架构：
#   Alice (SUB) ←→ GitHub Gist ←→ Bob (PUB)
#   - Bob: 发布者，主动发起连接（--to alice）
#   - Alice: 订阅者，被动等待连接（不指定 --to）
#   - Gist: 信令存储，轮询检测更新
#
# 使用方式：
#   ./test/test_pubsub_gist.sh
#
# ==============================================================================

set -e

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 工作目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$SCRIPT_DIR/pubsub_gist_logs"
mkdir -p "$LOG_DIR"

# 二进制文件
P2P_PING="$PROJECT_ROOT/build_cmake/p2p_ping/p2p_ping"

# GitHub 配置 (从 local/github_token.md 读取或使用环境变量)
if [ -z "$P2P_GITHUB_TOKEN" ] && [ -f "$PROJECT_ROOT/local/github_token.md" ]; then
    P2P_GITHUB_TOKEN=$(grep "ghp_" "$PROJECT_ROOT/local/github_token.md" | head -1)
fi
if [ -z "$P2P_GIST_ID" ] && [ -f "$PROJECT_ROOT/local/github_token.md" ]; then
    P2P_GIST_ID=$(grep "Gist ID:" "$PROJECT_ROOT/local/github_token.md" | cut -d':' -f2 | tr -d ' ')
fi

# 检查环境变量
if [ -z "$P2P_GITHUB_TOKEN" ] || [ -z "$P2P_GIST_ID" ]; then
    echo -e "${RED}错误: 缺少 GitHub 配置${NC}"
    echo "请设置环境变量："
    echo "  export P2P_GITHUB_TOKEN='ghp_xxx...'"
    echo "  export P2P_GIST_ID='gist_id'"
    echo ""
    echo "或者运行:"
    echo "  source ./local/p2p_github.sh"
    exit 1
fi

echo -e "${BLUE}=== P2P PUBSUB (GitHub Gist) 模式测试 ===${NC}"
echo ""
echo "GitHub Token: ${P2P_GITHUB_TOKEN:0:10}..."
echo "Gist ID: $P2P_GIST_ID"
echo ""

# 检查二进制文件
if [ ! -f "$P2P_PING" ]; then
    echo -e "${RED}错误: $P2P_PING 不存在${NC}"
    echo "请先编译项目: cd build_cmake && cmake .. && make"
    exit 1
fi

# 清理函数
cleanup() {
    echo ""
    echo -e "${YELLOW}清理进程...${NC}"
    pkill -f "p2p_ping.*alice" 2>/dev/null || true
    pkill -f "p2p_ping.*bob" 2>/dev/null || true
    wait 2>/dev/null || true
    
    # 清理 Gist 内容（可选）
    # curl -s -X PATCH \
    #      -H "Authorization: token $P2P_GITHUB_TOKEN" \
    #      -H "Content-Type: application/json" \
    #      -d '{"files":{"p2p_signal.json":{"content":"{}"}}}' \
    #      "https://api.github.com/gists/$P2P_GIST_ID" > /dev/null
}

trap cleanup EXIT INT TERM

# ==============================================================================
# 测试 1: 基本 PUBSUB 连通性测试
# ==============================================================================
test_pubsub_basic_connectivity() {
    echo -e "${YELLOW}=== 测试 1: PUBSUB 基本连通性 ===${NC}"
    echo ""
    
    # 清理旧日志
    rm -f "$LOG_DIR/pubsub_alice.log" "$LOG_DIR/pubsub_bob.log"
    
    # 清空 Gist (重置信令状态)
    echo "清空 Gist 信令通道..."
    curl -s -X PATCH \
         -H "Authorization: token $P2P_GITHUB_TOKEN" \
         -H "Content-Type: application/json" \
         -d '{"files":{"p2p_signal.json":{"content":"{}"}}}' \
         "https://api.github.com/gists/$P2P_GIST_ID" > /dev/null
    
    sleep 2
    
    # 启动 Alice (订阅者 - SUB，被动等待)
    echo "启动 Alice (SUB - 订阅者，等待连接)..."
    $P2P_PING \
        --name alice \
        --github "$P2P_GITHUB_TOKEN" \
        --gist "$P2P_GIST_ID" \
        > "$LOG_DIR/pubsub_alice.log" 2>&1 &
    ALICE_PID=$!
    
    # 等待 Alice 初始化
    sleep 3
    echo "Alice 进程启动: PID=$ALICE_PID"
    
    # 启动 Bob (发布者 - PUB，主动连接)
    echo "启动 Bob (PUB - 发布者，主动连接到 alice)..."
    $P2P_PING \
        --name bob \
        --to alice \
        --github "$P2P_GITHUB_TOKEN" \
        --gist "$P2P_GIST_ID" \
        > "$LOG_DIR/pubsub_bob.log" 2>&1 &
    BOB_PID=$!
    
    echo "Bob 进程启动: PID=$BOB_PID"
    echo ""
    
    # 等待连接建立（PUBSUB 模式需要更长时间，因为轮询延迟）
    echo "等待 P2P 连接建立 (最多 60 秒)..."
    echo "  - Bob 正在发布 offer 到 Gist..."
    echo "  - Alice 正在轮询检测 offer..."
    echo "  - Alice 将发布 answer..."
    echo "  - Bob 轮询检测 answer..."
    echo "  - 开始 ICE 协商..."
    echo ""
    
    TIMEOUT=60
    CONNECTED=0
    
    for i in $(seq 1 $TIMEOUT); do
        # 检查进程是否还存活
        if ! kill -0 $ALICE_PID 2>/dev/null; then
            echo -e "${RED}✗ Alice 进程已退出${NC}"
            break
        fi
        if ! kill -0 $BOB_PID 2>/dev/null; then
            echo -e "${RED}✗ Bob 进程已退出${NC}"
            break
        fi
        
        # 检查连接状态
        if grep -q "Connection established" "$LOG_DIR/pubsub_alice.log" 2>/dev/null && \
           grep -q "Connection established" "$LOG_DIR/pubsub_bob.log" 2>/dev/null; then
            CONNECTED=1
            echo -e "${GREEN}✓ 连接建立成功！(耗时: ${i}s)${NC}"
            break
        fi
        
        # 每 5 秒显示进度
        if [ $((i % 5)) -eq 0 ]; then
            echo "  等待中... ($i/$TIMEOUT 秒)"
        fi
        
        sleep 1
    done
    
    if [ $CONNECTED -eq 0 ]; then
        echo -e "${RED}✗ 测试失败: 连接超时${NC}"
        echo ""
        echo "Alice 日志 (最后 30 行):"
        tail -30 "$LOG_DIR/pubsub_alice.log"
        echo ""
        echo "Bob 日志 (最后 30 行):"
        tail -30 "$LOG_DIR/pubsub_bob.log"
        return 1
    fi
    
    # 验证数据传输
    echo ""
    echo "验证数据传输..."
    sleep 3
    
    if grep -q "Received: P2P_PING" "$LOG_DIR/pubsub_alice.log" || \
       grep -q "Received: P2P_PING" "$LOG_DIR/pubsub_bob.log"; then
        echo -e "${GREEN}✓ 数据传输正常${NC}"
    else
        echo -e "${YELLOW}⚠ 未检测到数据传输${NC}"
    fi
    
    # 显示详细信息
    echo ""
    echo -e "${BLUE}=== 连接详情 ===${NC}"
    
    # 检查 Alice 状态
    if grep -q "STATE.*CONNECTED" "$LOG_DIR/pubsub_alice.log"; then
        echo -e "${GREEN}Alice: CONNECTED${NC}"
        grep "STATE.*CONNECTED" "$LOG_DIR/pubsub_alice.log" | head -1
    fi
    
    # 检查 Bob 状态
    if grep -q "STATE.*CONNECTED" "$LOG_DIR/pubsub_bob.log"; then
        echo -e "${GREEN}Bob: CONNECTED${NC}"
        grep "STATE.*CONNECTED" "$LOG_DIR/pubsub_bob.log" | head -1
    fi
    
    # 检查 Gist 信令交换
    echo ""
    echo -e "${BLUE}=== Gist 信令交换 ===${NC}"
    
    # 从日志中提取关键信息
    if grep -q "PUBSUB" "$LOG_DIR/pubsub_bob.log"; then
        echo "Bob (PUB) 信令活动:"
        grep "PUBSUB\|Gist\|offer\|answer" "$LOG_DIR/pubsub_bob.log" | head -5
    fi
    
    echo ""
    
    if grep -q "PUBSUB" "$LOG_DIR/pubsub_alice.log"; then
        echo "Alice (SUB) 信令活动:"
        grep "PUBSUB\|Gist\|offer\|answer" "$LOG_DIR/pubsub_alice.log" | head -5
    fi
    
    # 清理
    kill $ALICE_PID $BOB_PID 2>/dev/null || true
    wait 2>/dev/null || true
    
    echo ""
    echo -e "${GREEN}✓ 测试 1 通过${NC}"
    echo ""
    
    return 0
}

# ==============================================================================
# 测试 2: Gist API 速率限制检查
# ==============================================================================
test_gist_rate_limit() {
    echo -e "${YELLOW}=== 测试 2: GitHub API 速率限制检查 ===${NC}"
    echo ""
    
    # 查询剩余配额
    RATE_LIMIT=$(curl -s \
        -H "Authorization: token $P2P_GITHUB_TOKEN" \
        https://api.github.com/rate_limit)
    
    REMAINING=$(echo "$RATE_LIMIT" | grep -o '"remaining":[0-9]*' | head -1 | cut -d':' -f2)
    LIMIT=$(echo "$RATE_LIMIT" | grep -o '"limit":[0-9]*' | head -1 | cut -d':' -f2)
    
    echo "API 速率限制:"
    echo "  总配额: $LIMIT 请求/小时"
    echo "  剩余配额: $REMAINING 请求"
    echo ""
    
    if [ "$REMAINING" -lt 100 ]; then
        echo -e "${YELLOW}⚠ 警告: API 配额不足 (<100 请求)${NC}"
        echo "  建议等待配额重置后再进行大规模测试"
    else
        echo -e "${GREEN}✓ API 配额充足${NC}"
    fi
    
    echo ""
    echo -e "${GREEN}✓ 测试 2 通过${NC}"
    echo ""
    
    return 0
}

# ==============================================================================
# 运行所有测试
# ==============================================================================

echo -e "${BLUE}=== 开始测试 ===${NC}"
echo ""

FAILED_TESTS=0

# 运行测试
test_pubsub_basic_connectivity || FAILED_TESTS=$((FAILED_TESTS + 1))
test_gist_rate_limit || FAILED_TESTS=$((FAILED_TESTS + 1))

# 总结
echo ""
echo -e "${BLUE}═══════════════════════════════════════════════════${NC}"
if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}✓ 所有测试通过 (2/2)${NC}"
    echo ""
    echo "日志位置: $LOG_DIR/"
    echo "  - pubsub_alice.log: Alice (SUB) 日志"
    echo "  - pubsub_bob.log: Bob (PUB) 日志"
    exit 0
else
    echo -e "${RED}✗ 失败: $FAILED_TESTS 个测试${NC}"
    exit 1
fi
