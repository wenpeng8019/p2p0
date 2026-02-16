# NAT 打洞流程测试指南

## 概述

本文档说明如何使用新增的 NAT 打洞测试功能，包括禁用同子网直连优化和详细日志输出。

## 新增功能

### 1. 配置选项 (`p2p.h`)

在 `p2p_config_t` 中添加了两个测试选项：

```c
typedef struct {
    // ... 其他配置 ...
    
    /* 测试选项 */
    int disable_lan_shortcut;    // 1 = 禁止同子网直连优化 (用于测试 NAT 打洞)
    int verbose_nat_punch;        // 1 = 输出详细的 NAT 打洞流程日志
} p2p_config_t;
```

### 2. 命令行选项 (`p2p_ping`)

在 `p2p_ping` 工具中添加了两个命令行选项：

- `--disable-lan`: 禁用 LAN shortcut（同子网直连），强制执行 NAT 打洞
- `--verbose-punch`: 启用详细的 NAT 打洞流程日志

### 3. 详细日志输出

启用 `--verbose-punch` 后，会输出以下日志标记：

- `[NAT_PUNCH] START`: 开始注册到信令服务器
- `[NAT_PUNCH] REGISTERING`: 重试注册（等待 PEER_INFO）
- `[NAT_PUNCH] PEER_INFO`: 收到对方地址信息
- `[NAT_PUNCH] STATE`: 状态转换（REGISTERING → PUNCHING）
- `[NAT_PUNCH] PUNCHING`: 发送打洞包尝试
- `[NAT_PUNCH] PUNCH`: 收到对方打洞包
- `[NAT_PUNCH] PUNCH_ACK`: 收到打洞应答
- `[NAT_PUNCH] SUCCESS`: 打洞成功！
- `[NAT_PUNCH] TIMEOUT`: 打洞超时，切换到中继模式

## 使用方法

### 自动化测试脚本

#### 1. 完整测试套件

运行完整的 NAT 打洞测试（包括 SIMPLE 和 Relay 模式）：

```bash
cd test
./test_nat_punch.sh
```

测试结果：
- ✅ 测试 SIMPLE 模式 NAT 打洞
- ✅ 测试 Relay 模式 NAT 打洞
- 日志保存在 `test/nat_punch_logs/`

#### 2. 快速测试脚本

在新终端窗口中测试（适合调试）：

```bash
cd test

# 测试 SIMPLE 模式
./quick_test_nat_punch.sh simple

# 测试 Relay 模式
./quick_test_nat_punch.sh relay
```

### 手动测试

#### SIMPLE 模式测试

**终端 1 (服务器)**:
```bash
./build_cmake/p2p_server/p2p_server 8888
```

**终端 2 (Alice)**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name alice \
  --to bob \
  --disable-lan \
  --verbose-punch
```

**终端 3 (Bob)**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --simple \
  --name bob \
  --to alice \
  --disable-lan \
  --verbose-punch
```

#### Relay 模式测试

**终端 1 (服务器)**:
```bash
./build_cmake/p2p_server/p2p_server 8888
```

**终端 2 (Alice - 被动方)**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --name alice \
  --disable-lan \
  --verbose-punch
```

**终端 3 (Bob - 主动方)**:
```bash
./build_cmake/p2p_ping/p2p_ping \
  --server 127.0.0.1 \
  --name bob \
  --to alice \
  --disable-lan \
  --verbose-punch
```

## 日志输出示例

### SIMPLE 模式典型日志流程

```
[TEST] LAN shortcut disabled - forcing NAT punch
[TEST] Verbose NAT punch logging enabled
[NAT_PUNCH] START: Registering 'alice' -> 'bob' with server 127.0.0.1:8888
[CONNECT] SIMPLE mode: registering <alice → bob>
[STATE] UNKNOWN (-1) -> REGISTERING (1)

[NAT_PUNCH] PEER_INFO: Received peer address
            Public:  127.0.0.1:53237
            Private: 127.0.0.1:53237
[NAT_PUNCH] STATE: REGISTERING -> PUNCHING

[NAT_PUNCH] PUNCHING: Attempt #1 to 127.0.0.1:53237
[STATE] REGISTERING (1) -> PUNCHING (2)

[NAT_PUNCH] PUNCH_ACK: Received from 127.0.0.1:53237
[NAT_PUNCH] SUCCESS: Hole punched! Connected to 127.0.0.1:53237
            Attempts: 1, Time: 12 ms

[EVENT] Connection established!
[STATE] PUNCHING (2) -> CONNECTED (3)
[DATA] Sent PING
[DATA] Received: P2P_PING_ALIVE
```

### 关键指标

从日志中可以提取以下关键指标：

- **打洞尝试次数**: `PUNCHING: Attempt #N`
- **打洞用时**: `Time: N ms`
- **收到的打洞包**: `PUNCH: Received`
- **收到的应答包**: `PUNCH_ACK: Received`
- **最终状态**: `SUCCESS` 或 `TIMEOUT`

## 测试场景

### 1. 本地测试（同子网）

使用 `--disable-lan` 选项强制 NAT 打洞，即使在本地也能测试完整流程：

```bash
# 禁用 LAN shortcut，强制走 NAT 打洞
./p2p_ping --disable-lan --verbose-punch ...
```

### 2. 跨网络测试

在不同网络环境下测试（需要公网服务器）：

```bash
# 机器 A
./p2p_ping --server <公网服务器IP> --simple --name alice --to bob --verbose-punch

# 机器 B
./p2p_ping --server <公网服务器IP> --simple --name bob --to alice --verbose-punch
```

### 3. 验证不同信令模式

测试不同信令服务下的打洞流程：

- **SIMPLE 模式**: 简单 UDP 信令
  ```bash
  ./p2p_ping --server 127.0.0.1 --simple --verbose-punch ...
  ```

- **Relay 模式**: TCP 有状态信令
  ```bash
  ./p2p_ping --server 127.0.0.1 --verbose-punch ...  # 默认 ICE
  ```

## 故障排查

### 打洞失败

如果看到 `[NAT_PUNCH] TIMEOUT`，检查：

1. **服务器是否运行**: `netstat -an | grep 8888`
2. **防火墙设置**: 确保端口未被阻止
3. **NAT 类型**: 使用 STUN 检测 NAT 类型
4. **对方是否在线**: 检查服务器日志

### 日志未输出

确保使用了 `--verbose-punch` 选项：

```bash
./p2p_ping --verbose-punch ...
```

### LAN shortcut 仍然生效

确保使用了 `--disable-lan` 选项：

```bash
./p2p_ping --disable-lan ...
```

## 相关文件

- **测试脚本**:
  - `test/test_nat_punch.sh` - 自动化测试套件
  - `test/quick_test_nat_punch.sh` - 快速手动测试

- **源代码**:
  - `include/p2p.h` - 配置选项定义
  - `src/p2p_nat.c` - NAT 打洞逻辑和详细日志
  - `src/p2p_nat.h` - NAT 上下文结构
  - `src/p2p.c` - LAN shortcut 控制逻辑
  - `p2p_ping/p2p_ping.c` - 命令行工具

## 性能指标

在本地测试环境下（MacOS, localhost）：

- **SIMPLE 模式打洞时间**: ~10-20 ms
- **Relay 模式协商时间**: ~100-200 ms
- **打洞尝试次数**: 通常 1-2 次即可成功

## 下一步

1. 在真实跨网络环境测试
2. 测试不同 NAT 类型组合
3. 收集生产环境性能数据
4. 优化打洞超时和重试策略

## 总结

新增的 NAT 打洞测试功能提供了：

✅ **可控测试环境**: 通过 `--disable-lan` 在本地模拟跨网络场景  
✅ **详细流程日志**: 通过 `--verbose-punch` 观察完整打洞流程  
✅ **自动化测试**: 使用脚本验证不同信令模式  
✅ **性能指标**: 记录打洞尝试次数和用时  

这些功能使得在本地就能完整测试和调试 NAT 打洞流程，无需复杂的跨网络测试环境。
