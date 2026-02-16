#!/bin/bash
# Quick test for Relay mode

cd /Users/wenpeng/dev/c/p2p

# Start server
echo "Starting server..."
./build_cmake/p2p_server/p2p_server 8888 > /tmp/test_relay_server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Start Alice (passive)
echo "Starting Alice (Relay mode, passive)..."
./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --name alice > /tmp/test_relay_alice.log 2>&1 &
ALICE_PID=$!
sleep 2

# Start Bob (active, connect to alice)
echo "Starting Bob (Relay mode, active)..."
./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --name bob --to alice > /tmp/test_relay_bob.log 2>&1 &
BOB_PID=$!

# Wait and collect logs
echo "Waiting 15 seconds..."
sleep 15

echo ""
echo "=== Server Log ==="
head -30 /tmp/test_relay_server.log

echo ""
echo "=== Alice Log ==="
cat /tmp/test_relay_alice.log

echo ""
echo "=== Bob Log ==="
cat /tmp/test_relay_bob.log

# Cleanup
kill $SERVER_PID $ALICE_PID $BOB_PID 2>/dev/null || true
echo ""
echo "Test completed"
