#!/bin/bash
cd /Users/wenpeng/dev/c/p2p

# 清理旧进程
pkill -f p2p_server 2>/dev/null
pkill -f p2p_ping 2>/dev/null
sleep 1

echo "=== Starting Server ==="
./build_cmake/p2p_server/p2p_server -p 9993 2>&1 &
SRV_PID=$!
sleep 1
echo "Server PID: $SRV_PID"

echo ""
echo "=== Round 1: Start alice and bob ==="
./build_cmake/p2p_ping/p2p_ping --compact -s 127.0.0.1:9993 -n alice -t bob 2>&1 &
A1_PID=$!
echo "Alice PID: $A1_PID"
./build_cmake/p2p_ping/p2p_ping --compact -s 127.0.0.1:9993 -n bob -t alice 2>&1 &
B1_PID=$!
echo "Bob PID: $B1_PID"
sleep 4
echo "Round 1 clients running for 4s..."

echo ""
echo "=== Killing Round 1 clients (SIGKILL - simulating crash) ==="
kill -9 $A1_PID $B1_PID 2>/dev/null
wait $A1_PID $B1_PID 2>/dev/null
echo "Killed alice ($A1_PID) and bob ($B1_PID)"
sleep 2

echo ""
echo "=== Round 2: Start alice and bob again ==="
./build_cmake/p2p_ping/p2p_ping --compact -s 127.0.0.1:9993 -n alice -t bob 2>&1 &
A2_PID=$!
echo "Alice PID: $A2_PID"
./build_cmake/p2p_ping/p2p_ping --compact -s 127.0.0.1:9993 -n bob -t alice 2>&1 &
B2_PID=$!
echo "Bob PID: $B2_PID"
sleep 6
echo "Round 2 clients running for 6s..."

echo ""
echo "=== Cleanup ==="
kill $A2_PID $B2_PID $SRV_PID 2>/dev/null
wait $A2_PID $B2_PID $SRV_PID 2>/dev/null
echo "Done"
