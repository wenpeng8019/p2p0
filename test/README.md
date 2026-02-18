# P2P Zero 测试指南

## 概述

P2P Zero 测试体系分为三个层次：
1. **单元测试** - 协议层和服务器端 C 测试
2. **集成测试** - 端到端 Shell 脚本测试
3. **专项测试** - NAT 打洞、STUN 等专项验证

## 测试体系

### 单元测试（9个 .c）

| 测试文件 | 测试目标 | 说明 |
|---------|---------|-----|
| `test_compact_protocol.c` | COMPACT 协议层 | UDP 包格式、状态机 |
| `test_compact_server.c` | COMPACT 服务器端 | Peer 匹配、注册逻辑 |
| `test_relay_protocol.c` | RELAY 协议层 | CONNECT_ACK 三状态、边界条件 |
| `test_relay_server.c` | RELAY 服务器端 | 连接管理、消息转发 |
| `test_pubsub_protocol.c` | PUBSUB 协议层 | DES 加密、JSON 序列化 |
| `test_transport.c` | 传输层 | ARQ 重传、流控、分片重组 |
| `test_stun_protocol.c` | STUN 协议层 | 包格式、XOR 解码、NAT 类型检测逻辑 |
| `test_ice_protocol.c` | ICE 协议层 | 优先级计算、候选收集、checklist 状态流转 |
| `test_turn_protocol.c` | TURN 协议层 | Allocate 请求包、成功响应解析 |

**运行方式**：
```bash
cd build_cmake
make
ctest -V
```

**测试覆盖**：
- `test_relay_protocol.c`：验证 RELAY 模式的 4 个边界条件
  - 边界1：对端在线（status=0）
  - 边界2：对端离线，有剩余空间（status=1）
  - 边界3：部分缓存后满（status=2, acked>0）
  - 边界4：缓存已满（status=2, acked=0）

- `test_pubsub_protocol.c`：验证 PUBSUB 模式的关键功能
  - DES 加密/解密（TODO: 集成实际实现）
  - JSON 候选序列化/解析
  - Gist API 响应格式验证

---

### 集成测试（6个 .sh）

| 测试脚本 | 测试目标 | 说明 |
|---------|---------|-----|
| `test_compact_integration.sh` | COMPACT 模式 | NAT 打洞、P2P 连接 |
| `test_relay_integration.sh` | RELAY 模式 | 信令交换、候选转发 |
| `test_pubsub_integration.sh` | PUBSUB 模式 | GitHub Gist 信令 |
| `test_ice_integration.sh` | ICE 模式 | 候选收集、打洞、连接建立 |
| `test_turn_integration.sh` | TURN 中继 | Allocate、中继连接（需 TURN 服务器） |
| `test_all.sh` | 全量测试 | 运行所有集成测试 |

**运行方式**：
```bash
cd test
./test_compact_integration.sh  # 单个测试
./test_all.sh                  # 全量测试
```

**测试流程**：
```
1. 启动信令服务器（COMPACT/RELAY）或配置 Gist（PUBSUB）
2. 启动 Alice（被动等待）
3. 启动 Bob（主动连接）
4. 验证连接建立
5. 验证数据传输
6. 清理进程
```

**日志位置**：
- COMPACT: `compact_integration_logs/`
- RELAY: `relay_integration_logs/`
- PUBSUB: `pubsub_gist_logs/`

---

### 专项测试（2个 .sh）

| 测试脚本 | 测试目标 | 说明 |
|---------|---------|-----|
| `test_nat_punch.sh` | NAT 打洞流程 | 详细打洞日志、禁用 LAN shortcut |
| `test_stun_integration.sh` | STUN 黑盒测试 | 真实进程与服务器交互、NAT 类型检测 |

**NAT 打洞测试**：
```bash
./test_nat_punch.sh
```

验证项目：
- [x] 禁用 LAN shortcut（强制 NAT 打洞）
- [x] 详细打洞日志输出（`--verbose-punch`）
- [x] COMPACT 模式打洞成功
- [x] RELAY 模式打洞成功

**日志示例**：
```
[NAT_PUNCH] START: Registering 'alice' -> 'bob'
[NAT_PUNCH] PEER_INFO: Received peer address 127.0.0.1:53237
[NAT_PUNCH] STATE: REGISTERING -> PUNCHING
[NAT_PUNCH] PUNCHING: Attempt #1 to 127.0.0.1:53237
[NAT_PUNCH] PUNCH_ACK: Received from 127.0.0.1:53237
[NAT_PUNCH] SUCCESS: Hole punched! Attempts: 1, Time: 12 ms
```

---

## 快速开始

### 1. 编译项目

```bash
cd /Users/wenpeng/dev/c/p2p
mkdir -p build_cmake && cd build_cmake
cmake ..
make
```

### 2. 运行单元测试

```bash
cd build_cmake
ctest -V
```

**预期输出**：
```
100% tests passed, 0 tests failed out of 6

Total Test time (real) = 2.34 sec
```

### 3. 运行集成测试

```bash
cd ../test
./test_relay_integration.sh
```

**预期输出**：
```
========================================
Test Summary
========================================
Total:   1
Passed:  1
Failed:  0

All tests PASSED!
```

### 4. 运行全量测试

```bash
cd test
./test_all.sh
```

---

## 手动测试

### RELAY 模式手动测试

**终端 1 - 信令服务器**：
```bash
./build_cmake/p2p_server/p2p_server 8888
```

**终端 2 - Alice（被动等待）**：
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --name alice
```

**终端 3 - Bob（主动连接）**：
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --name bob \
  --to alice
```

**观察要点**：
- 服务器：显示 LOGIN、CONNECT、OFFER、ANSWER、FORWARD
- Alice：收到 OFFER 后发送 ANSWER
- Bob：发送 OFFER，收到 ANSWER 后建立连接

---

### COMPACT 模式手动测试

**终端 1 - 信令服务器**：
```bash
./build_cmake/p2p_server/p2p_server 8888
```

**终端 2 - Alice**：
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name alice \
  --to bob
```

**终端 3 - Bob**：
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name bob \
  --to alice
```

**观察要点**：
- 双方向服务器注册 `<alice→bob>` 和 `<bob→alice>`
- 服务器匹配后返回对方地址
- 开始 NAT 打洞（PUNCH/PUNCH_ACK）
- 打洞成功后建立 P2P 连接

---

### NAT 打洞详细日志测试

启用详细日志和禁用 LAN shortcut：

```bash
# Alice
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name alice \
  --to bob \
  --disable-lan \
  --verbose-punch

# Bob
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name bob \
  --to alice \
  --disable-lan \
  --verbose-punch
```

---

## 命令行选项

### p2p_ping 选项

```bash
基本选项:
  --server IP          信令服务器 IP 地址
  --name NAME          本端名称/ID
  --to TARGET          目标对端名称（主动连接）
  --simple             使用 COMPACT 模式（默认 RELAY）

测试选项:
  --disable-lan        禁用 LAN shortcut（强制 NAT 打洞）
  --verbose-punch      输出详细的 NAT 打洞日志

安全选项:
  --dtls               启用 DTLS (MbedTLS)
  --openssl            启用 DTLS (OpenSSL)
  --pseudo             启用 PseudoTCP

GitHub Gist 信令:
  --github TOKEN       GitHub Token
  --gist ID            Gist ID
```

### p2p_server 选项

```bash
./build_cmake/p2p_server/p2p_server <port>
```

服务器支持两种模式：
- **TCP 端口 8888**：RELAY 模式信令（有状态，需要 LOGIN）
- **UDP 端口 8888**：COMPACT 模式信令（无状态，peer 匹配）

---

## 故障排查

### 测试失败

**端口占用**：
```bash
# 检查端口
lsof -i :8888

# 清理进程
pkill -f p2p_server
pkill -f p2p_ping
```

**编译问题**：
```bash
cd build_cmake
make clean
cmake ..
make
```

**查看详细日志**：
```bash
# 集成测试日志
ls -lh relay_integration_logs/
cat relay_integration_logs/alice.log

# NAT 打洞日志
cat nat_punch_logs/alice.log | grep NAT_PUNCH
```

---

### 连接失败

**RELAY 模式**：
- 检查服务器是否运行：`netstat -an | grep 8888`
- 查看服务器日志：`cat relay_integration_logs/server.log`
- 确认 Alice 发送了 ANSWER：`grep "Sent answer" relay_integration_logs/alice.log`

**COMPACT 模式**：
- 检查双方是否都连接到服务器
- 查看 NAT 打洞日志：`grep "NAT_PUNCH" compact_integration_logs/alice.log`
- 确认收到 PEER_INFO：`grep "PEER_INFO" compact_integration_logs/alice.log`

---

## 测试覆盖矩阵

| 功能模块 | 协议层单元测试（.c） | 集成测试（.sh） | 专项测试（.sh） | 状态 |
|---------|---------------------|----------------|----------------|------|
| COMPACT 协议 | test_compact_protocol.c | test_compact_integration.sh | test_nat_punch.sh | ✅ |
| COMPACT 服务器 | test_compact_server.c | test_compact_integration.sh | - | ✅ |
| RELAY 协议 | test_relay_protocol.c | test_relay_integration.sh | - | ✅ |
| RELAY 服务器 | test_relay_server.c | test_relay_integration.sh | - | ✅ |
| PUBSUB 协议 | test_pubsub_protocol.c | test_pubsub_integration.sh | - | ⚠️ TODO |
| 传输层 | test_transport.c | - | - | ✅ |
| STUN 协议 | test_stun_protocol.c | - | test_stun_integration.sh | ✅ |
| ICE 协议 | test_ice_protocol.c | test_ice_integration.sh | test_nat_punch.sh | ✅ |
| TURN 协议 | test_turn_protocol.c | test_turn_integration.sh | - | ✅ |
| NAT 打洞 | - | test_compact_integration.sh | test_nat_punch.sh | ✅ |

**注**：
- PUBSUB 协议测试使用模拟实现，需要集成实际的 DES、cJSON、libcurl。
- TURN 集成测试需配置 `TURN_SERVER` 环境变量指向可用的 TURN 服务器，否则标记为 SKIP。

---

## 目录结构

```
test/
├── README.md                          # 本文档
├── test_framework.h                   # 单元测试框架
│
├── 单元测试 (6个 .c)
│   ├── test_compact_protocol.c
│   ├── test_compact_server.c
│   ├── test_relay_protocol.c
│   ├── test_relay_server.c
│   ├── test_pubsub_protocol.c
│   └── test_transport.c
│
├── 集成测试 (4个 .sh)
│   ├── test_compact_integration.sh
│   ├── test_relay_integration.sh
│   ├── test_pubsub_integration.sh
│   └── test_all.sh
│
├── 专项测试 (2个 .sh)
│   ├── test_nat_punch.sh
│   └── test_stun_integration.sh
│
├── 快速测试脚本
│   ├── quick_test.sh
│   ├── quick_test_compact.sh
│   ├── quick_test_relay.sh
│   ├── quick_test_pubsub.sh
│   └── quick_test_nat_punch.sh
│
├── 日志目录（.gitignore）
│   ├── compact_integration_logs/
│   ├── relay_integration_logs/
│   ├── pubsub_gist_logs/
│   └── nat_punch_logs/
│
└── CMakeLists.txt                     # 单元测试编译配置
```

---

## 测试检查清单

### 开发前检查
- [ ] 代码编译通过：`cd build_cmake && make`
- [ ] 单元测试通过：`cd build_cmake && ctest -V`

### 提交前检查
- [ ] 集成测试通过：`./test_all.sh`
- [ ] NAT 打洞测试通过：`./test_nat_punch.sh`
- [ ] 手动验证 RELAY 模式（3个终端）
- [ ] 手动验证 COMPACT 模式（3个终端）

### 发布前检查
- [ ] 所有自动化测试通过
- [ ] 跨网络测试验证（不同子网）
- [ ] 性能测试（连接时间、吞吐量）
- [ ] 安全测试（DTLS、认证）

---

## 常用测试命令速查

```bash
# 快速验证一切正常
cd build_cmake && ctest -V && cd ../test && ./test_all.sh

# 单独测试某个模式
./test_relay_integration.sh
./test_compact_integration.sh

# 查看 NAT 打洞详细流程
./test_nat_punch.sh
cat nat_punch_logs/alice.log | grep NAT_PUNCH

# 手动测试（3个终端）
# T1: ./build_cmake/p2p_server/p2p_server 8888
# T2: ./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --name alice
# T3: ./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --name bob --to alice

# 清理所有测试进程
pkill -f p2p_server; pkill -f p2p_ping

# 查看测试日志
ls -lh *_logs/
```

---

## 相关文档

- [../README.md](../README.md) - 项目主文档
- [../doc/ARCHITECTURE.md](../doc/ARCHITECTURE.md) - 架构设计
- [../doc/P2P_PROTOCOL.md](../doc/P2P_PROTOCOL.md) - 协议规范
- [../CONNECT_API.md](../CONNECT_API.md) - 连接 API 指南

---

**最后更新**: 2026-02-18  
**版本**: 2.0（测试重构版）
