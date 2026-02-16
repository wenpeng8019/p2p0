# P2P Zero 测试指南

## 概述

本目录包含 P2P Zero 项目的所有测试工具和脚本。测试分为五大类：
1. **客户端集成测试** - 验证 Relay 和 SIMPLE 模式的端到端功能
2. **NAT 打洞测试** - 验证 NAT 穿透流程和详细日志
3. **PubSub 模式测试** - 验证 GitHub Gist 信令通道功能
4. **调试工具** - STUN 诊断和综合功能测试
5. **单元测试** - 测试服务器和传输层组件

## 快速开始

```bash
# 1. 编译项目
cd /Users/wenpeng/dev/c/p2p
mkdir -p build_cmake && cd build_cmake
cmake ..
make

# 2. 运行自动化测试
cd ../test
./test_client_integration.sh      # 客户端集成测试
./test_nat_punch.sh                # NAT 打洞测试
```

---

## 📋 测试脚本清单

### 1. 客户端集成测试

#### `test_client_integration.sh`
**功能**: 完整的客户端集成测试套件，验证 ICE 和 SIMPLE 两种模式

**测试内容**:
- ✅ Relay 模式：信令交换、候选者收集、连接建立、数据传输
- ✅ SIMPLE 模式：NAT 打洞、连接建立、数据传输

**运行方式**:
```bash
./test_client_integration.sh
```

**预期输出**:
```
================================================
Test Summary
================================================
Total:   2
Passed:  2
Failed:  0

All tests PASSED!
```

**日志位置**: `integration_logs/`
- `relay_server.log`, `relay_alice.log`, `relay_bob.log`
- `simple_server.log`, `simple_alice.log`, `simple_bob.log`

---

#### `quick_test_relay.sh`
**功能**: 快速测试 Relay 模式（单次运行，查看详细日志）

**运行方式**:
```bash
./quick_test_relay.sh
```

**输出**: 
- 服务器日志
- Alice 和 Bob 的连接日志
- 15 秒后自动停止

**日志位置**: `/tmp/test_relay_*.log`

---

#### `quick_test_simple.sh`
**功能**: 快速测试 SIMPLE 模式（单次运行，查看详细日志）

**运行方式**:
```bash
./quick_test_simple.sh
```

**输出**:
- 服务器日志
- Alice 和 Bob 的连接日志
- 15 秒后自动停止

**日志位置**: `/tmp/test_*.log`

---

### 2. NAT 打洞测试

#### `test_nat_punch.sh`
**功能**: 完整的 NAT 打洞流程测试套件

**测试内容**:
- ✅ SIMPLE 模式 NAT 打洞（禁用 LAN shortcut）
- ✅ Relay 模式 NAT 打洞（禁用 LAN shortcut）
- ✅ 详细的打洞流程日志验证

**运行方式**:
```bash
./test_nat_punch.sh
```

**预期输出**:
```
================================================
Test Summary
================================================
Total:   2
Passed:  2
Failed:  0

All tests PASSED!
```

**日志位置**: `nat_punch_logs/`
- `simple_server.log`, `simple_alice.log`, `simple_bob.log`
- `relay_server.log`, `relay_alice.log`, `relay_bob.log`

**查看详细日志**:
```bash
# 查看 NAT 打洞流程
cat nat_punch_logs/simple_alice.log | grep NAT_PUNCH

# 查看完整流程
cat nat_punch_logs/simple_alice.log
```

---

#### `quick_test_nat_punch.sh`
**功能**: 在新终端窗口中手动测试 NAT 打洞（适合调试）

**运行方式**:
```bash
# 测试 SIMPLE 模式
./quick_test_nat_punch.sh simple

# 测试 Relay 模式
./quick_test_nat_punch.sh relay
```

**特点**:
- 在新终端窗口中启动服务器、Alice、Bob
- 实时查看详细的 NAT 打洞日志
- 适合观察流程和调试
- 按 Ctrl+C 停止

---

### 3. PubSub 模式测试

#### `quick_test_pubsub.sh`
**功能**: P2P PubSub (GitHub Gist) 快速测试，在新终端窗口启动

**运行方式**:
```bash
./quick_test_pubsub.sh
```

**特点**:
- 自动在新终端窗口中启动 Alice 和 Bob
- 使用 GitHub Gist 作为信令通道
- 适合快速验证 PubSub 功能

---

#### `test_pubsub_gist.sh`
**功能**: PubSub (GitHub Gist) 模式连通性完整测试

**运行方式**:
```bash
./test_pubsub_gist.sh
```

**测试内容**:
- ✅ GitHub Gist 信令通道连接
- ✅ PUB/SUB 角色协商和信令交换
- ✅ ICE 候选者收集和 NAT 穿透

**架构说明**:
```
Alice (SUB) ←→ GitHub Gist ←→ Bob (PUB)
- Bob: 发布者，主动发起连接（--to alice）
- Alice: 订阅者，被动等待连接（不指定 --to）
- Gist: 信令存储，轮询检测更新
```

---

#### `test_pubsub_simple.sh`
**功能**: 简单的 PubSub 功能验证测试

**运行方式**:
```bash
./test_pubsub_simple.sh
```

**说明**: 快速验证 PubSub 基本功能，适合开发时快速检查

---

### 4. 调试工具

#### `test_stun.sh`
**功能**: STUN 调试测试脚本，用于诊断 STUN 响应问题

**运行方式**:
```bash
./test_stun.sh
```

**用途**:
- 诊断 STUN 服务器响应问题
- 分析 STUN 绑定请求/响应流程
- 调试 NAT 类型检测

---

#### `quick_test.sh`
**功能**: P2P 功能快速综合测试

**运行方式**:
```bash
./quick_test.sh
```

**说明**: 综合性快速测试脚本，验证 P2P 核心功能

---

### 5. 单元测试

#### `test_relay_server`
**功能**: 测试 Relay 信令服务器协议

**编译**:
```bash
make test_relay_server
```

**运行**:
```bash
./test_relay_server
```

**测试内容**:
- 协议常量定义
- 消息类型映射
- 信令转发逻辑
- 数据包格式验证

---

#### `test_simple_server`
**功能**: 测试 SIMPLE 信令服务器

**编译**:
```bash
make test_simple_server
```

**运行**:
```bash
./test_simple_server
```

---

#### `test_transport`
**功能**: 测试传输层（reliable、stream）

**编译**:
```bash
make test_transport
```

**运行**:
```bash
./test_transport
```

---

## 🔧 手动测试

### Relay 模式手动测试

**终端 1 - 服务器**:
```bash
cd /Users/wenpeng/dev/c/p2p
./build_cmake/p2p_server/p2p_server 8888
```

**终端 2 - Alice (被动方)**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --name alice
```

**终端 3 - Bob (主动方)**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --name bob \
  --to alice
```

**观察要点**:
- 服务器：显示登录和信令转发
- Alice：等待连接，收到 offer 后发送 answer
- Bob：发送 offer，收到 answer 后建立连接
- 连接建立后双方可以发送/接收数据

---

### SIMPLE 模式手动测试

**终端 1 - 服务器**:
```bash
./build_cmake/p2p_server/p2p_server 8888
```

**终端 2 - Alice**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name alice \
  --to bob
```

**终端 3 - Bob**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name bob \
  --to alice
```

**观察要点**:
- 双方向服务器注册 <alice → bob> 和 <bob → alice>
- 服务器匹配后返回对方地址
- 开始 NAT 打洞
- 打洞成功后建立 P2P 连接

---

### NAT 打洞详细日志测试

**启用详细日志和禁用 LAN shortcut**:

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

**日志输出示例**:
```
[TEST] LAN shortcut disabled - forcing NAT punch
[NAT_PUNCH] START: Registering 'alice' -> 'bob' with server 127.0.0.1:8888
[NAT_PUNCH] PEER_INFO: Received peer address
            Public:  127.0.0.1:53237
            Private: 127.0.0.1:53237
[NAT_PUNCH] STATE: REGISTERING -> PUNCHING
[NAT_PUNCH] PUNCHING: Attempt #1 to 127.0.0.1:53237
[NAT_PUNCH] PUNCH_ACK: Received from 127.0.0.1:53237
[NAT_PUNCH] SUCCESS: Hole punched! Connected to 127.0.0.1:53237
            Attempts: 1, Time: 12 ms
```

---

## 📊 命令行选项参考

### p2p_ping 选项

```bash
./build_cmake/p2p_ping/p2p_ping [options]

基本选项:
  --server IP          信令服务器 IP 地址
  --name NAME          本端名称/ID
  --to TARGET          目标对端名称（主动连接）
  --simple             使用 SIMPLE 模式（默认 ICE）

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
  
其他:
  --help               显示帮助信息
```

### p2p_server 选项

```bash
./build_cmake/p2p_server/p2p_server <port>

示例:
  ./build_cmake/p2p_server/p2p_server 8888
```

服务器支持两种模式：
- **TCP 端口 8888**: Relay 模式信令（有状态，需要登录）
- **UDP 端口 8888**: SIMPLE 模式信令（无状态，peer 匹配）

---

## 🐛 故障排查

### 测试失败

**端口占用**:
```bash
# 检查端口
lsof -i :8888

# 清理进程
pkill -f p2p_server
pkill -f p2p_ping
```

**编译问题**:
```bash
# 重新编译
cd build_cmake
make clean
cmake ..
make
```

**查看详细日志**:
```bash
# 集成测试日志
ls -lh integration_logs/
cat integration_logs/relay_alice.log

# NAT 打洞日志
ls -lh nat_punch_logs/
cat nat_punch_logs/simple_alice.log | grep NAT_PUNCH
```

### 连接失败

**Relay 模式**:
- 检查服务器是否运行：`netstat -an | grep 8888`
- 查看服务器日志：`cat integration_logs/relay_server.log`
- 确认 Alice 发送了 answer：`grep "Sent answer" integration_logs/relay_alice.log`

**SIMPLE 模式**:
- 检查双方是否都连接到服务器
- 查看 NAT 打洞日志：`grep "NAT_PUNCH" integration_logs/simple_alice.log`
- 确认收到 PEER_INFO：`grep "PEER_INFO" integration_logs/simple_alice.log`

---

## 📁 目录结构

```
test/
├── README.md                              # 本文档
├── NAT_PUNCH_TEST_GUIDE.md               # NAT 打洞详细指南
├── NAT_PUNCH_IMPLEMENTATION_SUMMARY.md   # 实现总结
├── TESTING.md                             # 完整测试文档
│
├── test_client_integration.sh             # ✅ 客户端集成测试（自动化）
├── test_nat_punch.sh                      # ✅ NAT 打洞测试（自动化）
│
├── quick_test_ice.sh                      # 🔍 ICE 快速测试
├── quick_test_simple.sh                   # 🔍 SIMPLE 快速测试
├── quick_test_nat_punch.sh                # 🔍 NAT 打洞手动测试
│
├── test_relay_server.c                      # 单元测试：ICE 服务器
├── test_simple_server.c                   # 单元测试：SIMPLE 服务器
├── test_transport.c                       # 单元测试：传输层
│
├── integration_logs/                      # 集成测试日志
├── nat_punch_logs/                        # NAT 打洞测试日志
└── CMakeLists.txt                         # CMake 构建配置
```

---

## ✅ 测试检查清单

### 开发前检查
- [ ] 代码编译通过：`cd build_cmake && make`
- [ ] 单元测试通过：`./test_relay_server && ./test_transport`

### 提交前检查
- [ ] 客户端集成测试通过：`./test_client_integration.sh`
- [ ] NAT 打洞测试通过：`./test_nat_punch.sh`
- [ ] 手动验证 Relay 模式：启动服务器 + Alice + Bob
- [ ] 手动验证 SIMPLE 模式：启动服务器 + Alice + Bob

### 发布前检查
- [ ] 所有自动化测试通过
- [ ] 跨网络测试验证（不同子网）
- [ ] 性能测试（连接时间、吞吐量）
- [ ] 安全测试（DTLS、认证）

---

## 📖 相关文档

- [TESTING.md](TESTING.md) - 完整的测试文档和最佳实践
- [NAT_PUNCH_TEST_GUIDE.md](NAT_PUNCH_TEST_GUIDE.md) - NAT 打洞测试详细指南
- [NAT_PUNCH_IMPLEMENTATION_SUMMARY.md](NAT_PUNCH_IMPLEMENTATION_SUMMARY.md) - NAT 打洞功能实现总结
- [../ARCHITECTURE.md](../ARCHITECTURE.md) - 项目架构设计
- [../README.md](../README.md) - 项目主文档

---

## 🎯 常用测试命令速查

```bash
# 快速验证一切正常
./test_client_integration.sh

# 查看 NAT 打洞详细流程
./test_nat_punch.sh

# 手动测试（3个终端）
# T1: ./build_cmake/p2p_server/p2p_server 8888
# T2: ./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --name alice
# T3: ./build_cmake/p2p_ping/p2p_ping --server 127.0.0.1 --name bob --to alice

# 调试 NAT 打洞
./quick_test_nat_punch.sh simple

# 清理所有测试进程
pkill -f p2p_server; pkill -f p2p_ping

# 查看测试日志
ls -lh integration_logs/
ls -lh nat_punch_logs/
```

---

## 📞 支持

如遇问题，请：
1. 查看相关日志文件
2. 参考 [TESTING.md](TESTING.md) 故障排查章节
3. 提交 issue 附带日志和复现步骤

---

**最后更新**: 2026-02-15  
**版本**: 1.0
