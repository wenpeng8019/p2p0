#!/bin/bash

# P2P Server 远程部署脚本
# 用法: ./deploy_server.sh <user@remote_host> [remote_path] [password]
#   或: P2P_SSH_PASS=password ./deploy_server.sh <user@remote_host> [remote_path]

set -e

if [ $# -lt 1 ]; then
    echo "用法: $0 <user@remote_host> [remote_path] [password]"
    echo "   或: P2P_SSH_PASS=password $0 <user@remote_host> [remote_path]"
    echo ""
    echo "示例:"
    echo "  $0 root@your-server.com"
    echo "  $0 root@your-server.com /opt/p2p"
    echo "  P2P_SSH_PASS=yourpass $0 root@your-server.com"
    exit 1
fi

SSH_TARGET="$1"
REMOTE_PATH="${2:-~/p2p}"
SSH_PASS="${3:-$P2P_SSH_PASS}"

# 配置SSH命令（支持sshpass自动密码）
if [ -n "$SSH_PASS" ]; then
    if ! command -v sshpass &> /dev/null; then
        echo "错误: 提供了密码但sshpass未安装"
        echo "安装方法: brew install sshpass (macOS) 或 apt-get install sshpass (Linux)"
        exit 1
    fi
    SSH_CMD="sshpass -p '$SSH_PASS' ssh"
else
    SSH_CMD="ssh"
fi

echo "========================================="
echo "P2P Server 远程部署"
echo "========================================="
echo "远程服务器: $SSH_TARGET"
echo "远程路径:   $REMOTE_PATH"
echo ""

# 1. 连接到远程服务器并执行更新
echo "[1/5] 连接到远程服务器..."
eval "$SSH_CMD $SSH_TARGET" bash -s "$REMOTE_PATH" <<'ENDSSH'
set -e

REMOTE_PATH="$1"

echo "[2/5] 进入项目目录..."
cd "$REMOTE_PATH" || {
    echo "错误: 目录 $REMOTE_PATH 不存在"
    exit 1
}

echo "[3/5] 拉取最新代码..."
git pull origin main

echo "[4/5] 重新构建 p2p_server..."
chmod +x build.sh
./build.sh server --config Release

echo "[5/5] 重启 p2p_server..."
# 查找并终止现有的 p2p_server 进程
if pgrep -f "p2p_server" > /dev/null; then
    echo "发现运行中的 p2p_server，正在终止..."
    pkill -f "p2p_server" || true
    sleep 1
fi

# 启动新的 p2p_server（后台运行）
echo "启动 p2p_server..."
nohup ./build/p2p_server/p2p_server > p2p_server.log 2>&1 &
SERVER_PID=$!

sleep 1

# 验证服务器是否启动成功
if ps -p $SERVER_PID > /dev/null; then
    echo "✓ p2p_server 启动成功 (PID: $SERVER_PID)"
    echo "  日志文件: $REMOTE_PATH/p2p_server.log"
else
    echo "✗ p2p_server 启动失败"
    echo "  请检查日志: $REMOTE_PATH/p2p_server.log"
    exit 1
fi

ENDSSH

echo ""
echo "========================================="
echo "部署完成！"
echo "========================================="
echo ""
if [ -n "$SSH_PASS" ]; then
    echo "查看日志: P2P_SSH_PASS='$SSH_PASS' ssh $SSH_TARGET 'tail -f $REMOTE_PATH/p2p_server.log'"
    echo "或直接: sshpass -p '$SSH_PASS' ssh $SSH_TARGET 'tail -f $REMOTE_PATH/p2p_server.log'"
    echo "查看进程: sshpass -p '$SSH_PASS' ssh $SSH_TARGET 'ps aux | grep p2p_server'"
    echo "停止服务: sshpass -p '$SSH_PASS' ssh $SSH_TARGET 'pkill -f p2p_server'"
else
    echo "查看日志: ssh $SSH_TARGET 'tail -f $REMOTE_PATH/p2p_server.log'"
    echo "查看进程: ssh $SSH_TARGET 'ps aux | grep p2p_server'"
    echo "停止服务: ssh $SSH_TARGET 'pkill -f p2p_server'"
fi
