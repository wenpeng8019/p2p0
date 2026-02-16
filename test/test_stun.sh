#!/bin/bash

# STUN 调试测试脚本
# 用于诊断 STUN 响应问题

set -e

cd "$(dirname "$0")/.."

echo "=== STUN 调试测试 ==="
echo ""

# 清理旧进程
pkill -9 p2p_server p2p_ping 2>/dev/null || true
sleep 1

# 清理旧日志
rm -f /tmp/stun_server.log /tmp/stun_alice.log

echo "1. 启动服务器..."
./build_cmake/p2p_server/p2p_server 8888 > /tmp/stun_server.log 2>&1 &
SERVER_PID=$!
echo "   服务器 PID: $SERVER_PID"
sleep 2

echo "2. 启动客户端 (Alice)..."
./build_cmake/p2p_ping/p2p_ping --name alice --server 127.0.0.1 > /tmp/stun_alice.log 2>&1 &
ALICE_PID=$!
echo "   客户端 PID: $ALICE_PID"

echo "3. 等待 30 秒收集日志..."
for i in {1..30}; do
    echo -n "."
    sleep 1
done
echo ""

echo "4. 停止进程..."
kill -9 $ALICE_PID $SERVER_PID 2>/dev/null || true
sleep 1

echo ""
echo "=== 服务器日志 ==="
cat /tmp/stun_server.log
echo ""

echo "=== Alice 日志（完整）==="
cat /tmp/stun_alice.log
echo ""

echo "=== Alice NAT/STUN 日志 ==="
grep -E "NAT|STUN|Mapped|Transaction|DEBUG" /tmp/stun_alice.log || echo "没有找到 NAT/STUN 相关日志"
echo ""

echo "=== 测试完成 ==="
