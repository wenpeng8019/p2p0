#!/bin/bash

# ==============================================================================
# P2P PUBSUB (GitHub Gist) 快速测试脚本
# ==============================================================================
#
# 自动在新终端窗口中启动 Alice 和 Bob，使用 GitHub Gist 作为信令通道
#
# 使用方式:
#   ./test/quick_test_pubsub.sh
#
# ==============================================================================

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

# 工作目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

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
    echo ""
    echo "请设置环境变量："
    echo "  export P2P_GITHUB_TOKEN='ghp_xxx...'"
    echo "  export P2P_GIST_ID='gist_id'"
    echo ""
    echo "或者编辑 local/p2p_github.sh 文件"
    exit 1
fi

echo -e "${BLUE}=== P2P PUBSUB 快速测试 ===${NC}"
echo ""
echo "GitHub Token: ${P2P_GITHUB_TOKEN:0:10}..."
echo "Gist ID: $P2P_GIST_ID"
echo ""

# 检查二进制文件
P2P_PING="$PROJECT_ROOT/build_cmake/p2p_ping/p2p_ping"
if [ ! -f "$P2P_PING" ]; then
    echo -e "${RED}错误: $P2P_PING 不存在${NC}"
    echo "请先编译项目: cd build_cmake && cmake .. && make"
    exit 1
fi

# 清理旧进程
echo "清理旧进程..."
pkill -f "p2p_ping.*alice" 2>/dev/null || true
pkill -f "p2p_ping.*bob" 2>/dev/null || true
sleep 1

# 清空 Gist 信令数据
echo "清空 Gist 信令通道..."
curl -s -X PATCH \
     -H "Authorization: token $P2P_GITHUB_TOKEN" \
     -H "Content-Type: application/json" \
     -d '{"files":{"p2p_signal.json":{"content":"{}"}}}' \
     "https://api.github.com/gists/$P2P_GIST_ID" > /dev/null

sleep 1

echo ""
echo -e "${GREEN}启动测试...${NC}"
echo ""

# 启动 Alice (订阅者 - SUB，被动等待)
echo "在新终端启动 Alice (SUB - 订阅者)..."
osascript -e "tell app \"Terminal\" to do script \"cd $PROJECT_ROOT && echo '=== ALICE (SUB - Subscriber) ===' && echo 'Waiting for connection from Bob...' && echo '' && ./local/p2p_github.sh ./build_cmake/p2p_ping/p2p_ping --name alice --github \$P2P_GITHUB_TOKEN --gist \$P2P_GIST_ID\""

sleep 3

# 启动 Bob (发布者 - PUB，主动连接)
echo "在新终端启动 Bob (PUB - 发布者)..."
osascript -e "tell app \"Terminal\" to do script \"cd $PROJECT_ROOT && echo '=== BOB (PUB - Publisher) ===' && echo 'Connecting to Alice via GitHub Gist...' && echo '' && ./local/p2p_github.sh ./build_cmake/p2p_ping/p2p_ping --name bob --to alice --github \$P2P_GITHUB_TOKEN --gist \$P2P_GIST_ID\""

echo ""
echo -e "${GREEN}✓ 测试已启动！${NC}"
echo ""
echo "请查看 2 个终端窗口："
echo ""
echo -e "${BLUE}📡 Alice (SUB - 订阅者):${NC}"
echo "  - 等待连接"
echo "  - 轮询 GitHub Gist 检测 offer"
echo "  - 收到 offer 后发布 answer"
echo "  - 完成 ICE 协商"
echo ""
echo -e "${BLUE}📡 Bob (PUB - 发布者):${NC}"
echo "  - 主动发起连接"
echo "  - 发布 offer 到 GitHub Gist"
echo "  - 轮询检测 answer"
echo "  - 完成 ICE 协商"
echo ""
echo -e "${YELLOW}观察要点:${NC}"
echo "  1. 角色分配: Alice=SUB, Bob=PUB"
echo "  2. Gist 信令交换 (offer/answer 通过 Gist 传递)"
echo "  3. 轮询延迟 (SUB:10s, PUB:5s)"
echo "  4. ICE 候选者收集"
echo "  5. NAT 穿透过程"
echo "  6. 连接建立时间 (预期 10-30 秒)"
echo "  7. 数据传输 (PING/PONG)"
echo ""
echo -e "${YELLOW}预期连接时间: 10-30 秒${NC}"
echo "  - Gist 轮询延迟: 5-15s"
echo "  - GitHub API 响应: 1-2s"
echo "  - ICE 协商: 2-5s"
echo ""
echo -e "${BLUE}提示:${NC}"
echo "  - 使用 Ctrl+C 停止测试"
echo "  - 查看完整文档: doc/GIST_SIGNALING_MECHANISM.md"
echo "  - 运行自动化测试: ./test/test_pubsub_gist.sh"
echo ""
