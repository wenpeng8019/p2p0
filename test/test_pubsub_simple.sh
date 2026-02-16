#!/bin/bash

# 简单的 pubsub 测试脚本
set -e

# 设置环境变量 (请替换为你自己的 Token 和 Gist ID)
export P2P_GITHUB_TOKEN="${P2P_GITHUB_TOKEN:-ghp_YOUR_TOKEN_HERE}"
export P2P_GIST_ID="${P2P_GIST_ID:-YOUR_GIST_ID_HERE}"

cd "$(dirname "$0")/.."

echo "=== Pubsub 简单测试 ==="
echo ""

# 清理
pkill -9 p2p_ping 2>/dev/null || true
rm -f /tmp/pub_alice.log /tmp/pub_bob.log

# 清空 Gist
echo "清空 Gist..."
curl -s -X PATCH \
  -H "Authorization: token $P2P_GITHUB_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"files":{"p2p_signal.json":{"content":"{}"}}}' \
  "https://api.github.com/gists/$P2P_GIST_ID" > /dev/null

echo "启动 Alice (SUB - passive)..."
./build_cmake/p2p_ping/p2p_ping \
  --github "$P2P_GITHUB_TOKEN" \
  --gist "$P2P_GIST_ID" \
  --name alice > /tmp/pub_alice.log 2>&1 &
ALICE_PID=$!

sleep 2

echo "启动 Bob (PUB - active)..."
./build_cmake/p2p_ping/p2p_ping \
  --github "$P2P_GITHUB_TOKEN" \
  --gist "$P2P_GIST_ID" \
  --name bob \
  --to alice > /tmp/pub_bob.log 2>&1 &
BOB_PID=$!

echo "等待 45 秒..."
for i in {1..45}; do
    echo -n "."
    sleep 1
    
    # 检查连接状态
    if grep -q "\[CONNECTED\]" /tmp/pub_alice.log 2>/dev/null; then
        echo ""
        echo "✓ Alice 已连接!"
        break
    fi
done
echo ""

# 停止
kill -9 $ALICE_PID $BOB_PID 2>/dev/null || true

echo ""
echo "=== Alice 日志 ==="
tail -50 /tmp/pub_alice.log

echo ""
echo "=== Bob 日志 ==="
tail -50 /tmp/pub_bob.log

echo ""
echo "=== 测试完成 ==="
