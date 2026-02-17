#!/bin/bash
# P2P 快速测试脚本
# 保存为 quick_test.sh 并执行: chmod +x quick_test.sh && ./quick_test.sh

set -e  # 遇到错误立即退出

cd /Users/wenpeng/dev/c/p2p

echo "========================================="
echo "  P2P 功能快速测试"
echo "========================================="
echo ""

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 检查可执行文件
echo "📋 检查构建产物..."
if [ ! -f "build_cmake/p2p_server/p2p_server" ]; then
    echo -e "${RED}❌ p2p_server 不存在，请先运行 cmake 构建${NC}"
    exit 1
fi

if [ ! -f "build_cmake/p2p_ping/p2p_ping" ]; then
    echo -e "${RED}❌ p2p_ping 不存在，请先运行 cmake 构建${NC}"
    exit 1
fi

echo -e "${GREEN}✓ 构建产物检查通过${NC}"
echo ""

# 测试选择菜单
echo "请选择要运行的测试："
echo ""
echo "  1) Relay 模式测试 (TCP 信令，需要3个终端)"
echo "  2) COMPACT 模式测试 (UDP 信令，NAT 打洞)"
echo "  3) NAT 打洞详细日志测试"
echo "  4) 手动测试命令参考"
echo "  5) 查看测试指南"
echo ""
read -p "请输入选项 [1-5]: " choice

case $choice in
    1)
        echo ""
        echo -e "${YELLOW}=== 测试 1: Relay 模式测试 ===${NC}"
        echo ""
        echo "这将启动："
        echo "  1. 信令服务器 (端口 8888, TCP)"
        echo "  2. Alice (被动方，等待连接)"
        echo "  3. Bob (主动方，连接到 Alice)"
        echo ""
        read -p "按回车继续..."
        
        # 检查端口 8888 是否已被占用
        if lsof -i :8888 > /dev/null 2>&1; then
            echo -e "${YELLOW}⚠ 端口 8888 已被占用，尝试清理...${NC}"
            pkill -f "p2p_server.*8888" || true
            sleep 1
        fi
        
        # 启动信令服务器
        echo ""
        echo "启动信令服务器..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== P2P SIGNALING SERVER (Relay Mode) ===\" && ./build_cmake/p2p_server/p2p_server 8888"'
        
        sleep 3
        
        # 启动 Alice
        echo "启动 Alice (被动方)..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== ALICE (Passive) ===\" && ./build_cmake/p2p_ping/p2p_ping --name alice --server 127.0.0.1"'
        
        sleep 2
        
        # 启动 Bob
        echo "启动 Bob (主动方)..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== BOB (Active) ===\" && ./build_cmake/p2p_ping/p2p_ping --name bob --server 127.0.0.1 --to alice"'
        
        echo ""
        echo -e "${GREEN}✓ 测试已启动！${NC}"
        echo ""
        echo "请查看 3 个终端窗口："
        echo "  📡 服务器: 应显示 TCP 连接和信令转发"
        echo "  👤 Alice: 等待连接，收到 offer"
        echo "  👤 Bob: 主动连接，发送 offer"
        echo ""
        echo "观察要点："
        echo "  - MSG_LOGIN: 客户端登录"
        echo "  - MSG_CONNECT: Bob 发起连接"
        echo "  - MSG_SIGNAL: 服务器转发给 Alice"
        echo "  - MSG_SIGNAL_ANS: Alice 回复 answer"
        echo "  - ICE 候选者收集和连接建立"
        echo ""
        ;;
        
    2)
        echo ""
        echo -e "${YELLOW}=== 测试 2: COMPACT 模式测试 ===${NC}"
        echo ""
        echo "这将启动："
        echo "  1. 信令服务器 (端口 8888, UDP)"
        echo "  2. Alice (连接到 Bob)"
        echo "  3. Bob (连接到 Alice)"
        echo ""
        read -p "按回车继续..."
        
        read -p "按回车继续..."
        
        # 检查端口 8888 是否已被占用
        if lsof -i :8888 > /dev/null 2>&1; then
            echo -e "${YELLOW}⚠ 端口 8888 已被占用，尝试清理...${NC}"
            pkill -f "p2p_server.*8888" || true
            sleep 1
        fi
        
        # 启动信令服务器
        echo ""
        echo "启动信令服务器..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== P2P SIGNALING SERVER (COMPACT Mode) ===\" && ./build_cmake/p2p_server/p2p_server 8888"'
        
        sleep 3
        
        # 启动 Alice
        echo "启动 Alice..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== ALICE ===\" && ./build_cmake/p2p_ping/p2p_ping --name alice --server 127.0.0.1 --compact --to bob"'
        
        sleep 2
        
        # 启动 Bob
        echo "启动 Bob..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== BOB ===\" && ./build_cmake/p2p_ping/p2p_ping --name bob --server 127.0.0.1 --compact --to alice"'
        
        echo -e "${YELLOW}=== 手动测试命令参考 ===${NC}"
        echo ""
        echo "你可以在不同终端手动运行以下命令："
        echo ""
        echo -e "${GREEN}# Relay 模式测试:${NC}"
        echo "# 终端 1: 启动服务器"
        echo "./build_cmake/p2p_server/p2p_server 8888"
        echo ""
        echo "# 终端 2: 启动 Alice (被动方)"
        echo "./build_cmake/p2p_ping/p2p_ping --name alice --server 127.0.0.1"
        echo ""
        echo "# 终端 3: 启动 Bob (主动方)"
        echo "./build_cmake/p2p_ping/p2p_ping --name bob --server 127.0.0.1 --to alice"
        echo ""
        echo -e "${GREEN}# COMPACT 模式测试:${NC}"
        echo "# 终端 1: 启动服务器"
        echo "./build_cmake/p2p_server/p2p_server 8888"
        echo ""
        echo "# 终端 2: 启动 Alice"
        echo "./build_cmake/p2p_ping/p2p_ping --name alice --server 127.0.0.1 --compact --to bob"
        echo ""
        echo "# 终端 3: 启动 Bob"
        echo "./build_cmake/p2p_ping/p2p_ping --name bob --server 127.0.0.1 --compact --to alice"
        echo ""
        echo -e "${GREEN}# NAT 打洞详细日志:${NC}"
        echo "./build_cmake/p2p_ping/p2p_ping --disable-lan --verbose-punch ..."
        echo ""
        echo -e "${GREEN}其他选项:${NC}"
        echo "  --dtls      启用 DTLS 加密 (MbedTLS)"
        echo "  --openssl   启用 DTLS 加密 (OpenSSL)"
        echo "  --pseudo    启用 PseudoTCP"
        echo ""
        ;;
        
    5)
        echo ""
        if [ -f "README.md" ]; then
            cat README.md | head -150
            echo ""
            echo "..."
            echo ""
            echo -e "${GREEN}完整测试指南请查看: test/README.md${NC}"
        else
            echo -e "${YELLOW}README.md 文件不存在${NC}"
            echo "请查看 TESTING.md 获取测试指南"
        fi
        read -p "按回车继续..."
        
        # 检查端口 8888 是否已被占用
        if lsof -i :8888 > /dev/null 2>&1; then
            echo -e "${YELLOW}⚠ 端口 8888 已被占用，尝试清理...${NC}"
            pkill -f "p2p_server.*8888" || true
            sleep 1
        fi
        
        # 启动信令服务器
        echo ""
        echo "启动信令服务器..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== P2P SERVER ===\" && ./build_cmake/p2p_server/p2p_server 8888"'
        
        sleep 3
        
        # 启动 Alice (带详细日志)
        echo "启动 Alice (带详细日志)..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== ALICE (Verbose Logs) ===\" && ./build_cmake/p2p_ping/p2p_ping --name alice --server 127.0.0.1 --compact --to bob --disable-lan --verbose-punch"'
        
        sleep 2
        
        # 启动 Bob (带详细日志)
        echo "启动 Bob (带详细日志)..."
        osascript -e 'tell app "Terminal" to do script "cd /Users/wenpeng/dev/c/p2p && echo \"=== BOB (Verbose Logs) ===\" && ./build_cmake/p2p_ping/p2p_ping --name bob --server 127.0.0.1 --compact --to alice --disable-lan --verbose-punch"'
        
        echo ""
        echo -e "${GREEN}✓ 测试已启动！${NC}"
        echo ""
        echo "查看日志中的 [NAT_PUNCH] 标记："
        echo "  [NAT_PUNCH] START: 开始注册"
        echo "  [NAT_PUNCH] PEER_INFO: 收到对方地址"
        echo "  [NAT_PUNCH] PUNCHING: 发送打洞包"
        echo "  [NAT_PUNCH] PUNCH_ACK: 收到应答"
        echo "  [NAT_PUNCH] SUCCESS: 打洞成功"
        echo ""
        ;;
        
    4)
        echo ""
        echo -e "${YELLOW}=== 手动测试命令参考 ===${NC}"
        echo ""
        echo "你可以在不同终端手动运行以下命令："
        echo ""
        echo "# 终端 1: 启动服务器"
        echo "./build_cmake/p2p_server/p2p_server 8888"
        echo ""
        echo "# 终端 2: 启动 Alice"
        echo "./build_cmake/p2p_ping/p2p_ping --name alice --server 127.0.0.1"
        echo ""
        echo "# 终端 3: 启动 Bob"
        echo "./build_cmake/p2p_ping/p2p_ping --name bob --server 127.0.0.1 --to alice"
        echo ""
        echo "其他选项："
        echo "  --dtls      启用 DTLS 加密"
        echo "  --pseudo    启用 PseudoTCP"
        echo "  --openssl   使用 OpenSSL DTLS"
        echo ""
        ;;
        
    4)
        echo ""
        cat TESTING_GUIDE.md | head -100
        echo ""
        echo "完整测试指南请查看: TESTING_GUIDE.md"
        echo ""
        ;;
        
    *)
        echo -e "${RED}无效选项${NC}"
        exit 1
        ;;
esac

echo ""
echo "========================================="
echo "  测试脚本执行完毕"
echo "========================================="
echo ""
echo "💡 提示："
echo "  - 按 Ctrl+C 可以停止任何进程"
echo "  - 查看日志: tail -f server.log"
echo "  - 清理进程: pkill -f p2p_ping"
echo "  - 完整指南: cat TESTING_GUIDE.md"
echo ""
