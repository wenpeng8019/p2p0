#!/bin/bash
# Quick test for COMPACT mode

cd /Users/wenpeng/dev/c/p2p

# Start server
echo "Starting server..."
./build_cmake/p2p_server/p2p_server 8888 > /tmp/test_server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Start Alice
echo "Starting Alice (COMPACT mode)..."
./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --compact --name alice --to bob > /tmp/test_alice.log 2>&1 &
ALICE_PID=$!
sleep 2

# Start Bob
echo "Starting Bob (COMPACT mode)..."
./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --compact --name bob --to alice > /tmp/test_bob.log 2>&1 &
BOB_PID=$!

# Wait and collect logs
echo "Waiting 10 seconds..."
sleep 10

echo ""
echo "=== Server Log ==="
cat /tmp/test_server.log

echo ""
echo "=== Alice Log ==="
cat /tmp/test_alice.log

echo ""
echo "=== Bob Log ==="
cat /tmp/test_bob.log

# Cleanup
kill $SERVER_PID $ALICE_PID $BOB_PID 2>/dev/null || true
echo ""
echo "Test completed"
