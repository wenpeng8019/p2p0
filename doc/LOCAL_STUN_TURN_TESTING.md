# 本地 STUN/TURN 服务测试指南

本文档描述如何在 macOS 上部署本地 STUN/TURN 服务器（Coturn），并测试完整的：
- **STUN**: RFC 3489/5389/5780 NAT 检测流程
- **TURN**: RFC 5766 中继分配流程

## 背景

公网 STUN 服务器（如 stun.l.google.com）通常只支持 RFC 5389 基础绑定请求，不支持 RFC 3489 的 CHANGE-REQUEST 属性，导致 NAT 类型检测的 Test II/III 永远超时。

要完整测试 NAT 类型检测和 TURN 中继，需要部署支持双 IP + 双端口 + 认证的本地服务器。

## 1. 安装 Coturn

```bash
brew install coturn
```

验证安装：
```bash
which turnserver
# 输出: /opt/homebrew/bin/turnserver
```

## 2. 配置双 IP

RFC 3489 NAT 检测需要服务器有两个不同的 IP 地址。在 macOS 上添加 IP 别名：

```bash
# 查看当前 IP
ifconfig en0 | grep "inet "
# 示例输出: inet 10.2.120.148 netmask 0xfffffe00 broadcast 10.2.121.255

# 添加第二个 IP（选择同网段的未使用地址）
sudo ifconfig en0 alias 10.2.120.149 netmask 255.255.255.0

# 验证双 IP
ifconfig en0 | grep "inet "
# inet 10.2.120.148 netmask 0xfffffe00 broadcast 10.2.121.255
# inet 10.2.120.149 netmask 0xffffff00 broadcast 10.2.120.255
```

> **注意：** IP 别名在系统重启后会丢失，需要重新添加。

## 3. 创建 Coturn 配置文件

创建 `~/turnserver.conf`：

```bash
cat > ~/turnserver.conf << 'EOF'
# Coturn STUN/TURN Server Configuration
# 支持 STUN (RFC 5389/5780) 和 TURN (RFC 5766)

# 双 IP 配置（STUN NAT 检测需要）
listening-ip=10.2.120.148
listening-ip=10.2.120.149

# STUN/TURN 端口
listening-port=3478
alt-listening-port=3479

# 中继地址范围（TURN relay 需要）
relay-ip=10.2.120.148
min-port=49152
max-port=65535

# RFC 5780 NAT 行为检测
rfc5780

# 认证配置（TURN 需要，STUN 无需认证）
realm=local
user=test:test123

# 调试日志
verbose

# 禁用 TLS（简化测试）
no-tls
no-dtls

# 日志输出到 stdout
log-file=stdout

# 指纹
fingerprint
EOF
```

**配置说明：**

| 配置项 | 用途 |
|--------|------|
| `listening-ip` x2 | 双 IP，支持 CHANGE-REQUEST |
| `alt-listening-port` | 备用端口（3479），支持 CHANGE-REQUEST |
| `relay-ip` | TURN 中继地址 |
| `min-port/max-port` | TURN 中继端口范围 |
| `rfc5780` | 启用 NAT 行为检测 |
| `user=test:test123` | TURN 认证用户（用户名:密码） |

## 4. 启动 Coturn

```bash
# 前台运行（查看日志）
turnserver -c ~/turnserver.conf

# 后台运行
turnserver -c ~/turnserver.conf &

# 停止
pkill turnserver
```

成功启动后应看到：
```
INFO: IPv4. UDP listener opened on: 10.2.120.148:3478
INFO: IPv4. UDP listener opened on: 10.2.120.148:3479
INFO: IPv4. UDP listener opened on: 10.2.120.149:3478
INFO: IPv4. UDP listener opened on: 10.2.120.149:3479
```

## 5. 验证 STUN 服务

### 5.1 使用 stuntman 测试

```bash
# 安装 stuntman
brew install stuntman

# 基础绑定测试
stunclient 10.2.120.148
# Binding test: success
# Local address: 10.2.120.148:xxxxx
# Mapped address: 10.2.120.148:xxxxx

# NAT 行为检测（RFC 5780）
stunclient --mode behavior 10.2.120.148
# Nat behavior: Direct Mapping

# 完整检测（行为 + 过滤）
stunclient --mode full 10.2.120.148
# Nat behavior: Direct Mapping
# Nat filtering: Endpoint Independent Filtering
```

### 5.2 使用 p2p_ping 测试 STUN

```bash
# 启动 signaling server
cd build_cmake/p2p_server
./p2p_server -r &

# 运行 p2p_ping 测试 STUN
cd build_cmake/p2p_ping
./p2p_ping --stun 10.2.120.148 -s localhost -n alice -l 5
```

正常输出示例：
```
[STUN] Sending Test I to 10.2.120.148:3478 (len=28)
[STUN] Test I: Mapped address: 10.2.120.148:52699
[STUN] Sending Test II with CHANGE-REQUEST(IP+PORT)
[STUN] Test II: Success! Detection completed Full Cone NAT
[STUN] Detection completed Full Cone NAT
```

### 5.3 使用 p2p_ping 测试 TURN

```bash
# 启动 signaling server（如未启动）
cd build_cmake/p2p_server
./p2p_server -r &

# 运行 p2p_ping 测试 STUN + TURN
cd build_cmake/p2p_ping
./p2p_ping \
  --stun 10.2.120.148 \
  --turn 10.2.120.148 \
  --turn-user test \
  --turn-pass test123 \
  -s localhost -n alice -l 5
```

正常输出示例：
```
[TURN] Sending Allocate Request to 10.2.120.148:3478
[TURN] TURN 401 Unauthorized (realm=local), authenticating...     ← 认证挑战（正常）
[TURN] Retrying Allocate with long-term credentials               ← 带凭证重试
[TURN] TURN Allocated relay 10.2.120.148:64151 (lifetime=600s)    ← 分配成功
[TURN] Gathered Relay Candidate 10.2.120.148:64151 (priority=16777215)
```

**TURN 参数说明：**

| 参数 | 说明 |
|------|------|
| `--turn` | TURN 服务器地址（默认端口 3478） |
| `--turn-user` | TURN 用户名 |
| `--turn-pass` | TURN 密码 |

## 6. NAT 检测流程说明

| 测试 | 发送目标 | CHANGE-REQUEST | 期望响应来源 | 检测目的 |
|------|----------|----------------|--------------|----------|
| Test I | IP1:Port1 | 无 | IP1:Port1 | 获取 Mapped Address |
| Test II | IP1:Port1 | IP+PORT | IP2:Port2 | 检测是否 Full Cone |
| Test III | IP1:Port1 | PORT | IP1:Port2 | 区分 NAT 类型 |

### NAT 类型判断：

- **Full Cone NAT**: Test II 成功
- **Restricted Cone NAT**: Test II 失败，Test III 成功
- **Port Restricted Cone NAT**: Test II/III 都失败，但 Test I 的 Mapped Address 一致
- **Symmetric NAT**: Mapped Address 随目标变化

## 7. TURN 协议流程说明

TURN (Traversal Using Relays around NAT) 用于在 P2P 直连失败时通过中继服务器转发数据。

### 7.1 TURN Allocate 流程

```
Client                                 TURN Server
   |                                        |
   |--- Allocate Request ------------------>|
   |                                        |
   |<-- 401 Unauthorized (realm, nonce) ----|  ← 认证挑战
   |                                        |
   |--- Allocate Request (with auth) ------>|  ← 带 MESSAGE-INTEGRITY
   |                                        |
   |<-- Allocate Success (relay addr) ------|  ← 返回中继地址
   |                                        |
```

### 7.2 TURN 消息类型

| 类型 | 代码 | 说明 |
|------|------|------|
| Allocate Request | 0x0003 | 请求分配中继地址 |
| Allocate Success | 0x0103 | 分配成功，包含 XOR-RELAYED-ADDRESS |
| Allocate Error | 0x0113 | 分配失败（401=需认证，438=Nonce过期） |
| Refresh Request | 0x0004 | 刷新分配（延长 lifetime） |
| Send Indication | 0x0016 | 通过中继发送数据 |
| Data Indication | 0x0017 | 从中继接收数据 |
| CreatePermission | 0x0008 | 为对端创建权限 |

### 7.3 认证机制

TURN 使用 Long-Term Credentials（RFC 5389）：
1. 服务器返回 401 + REALM + NONCE
2. 客户端计算：`key = MD5(username:realm:password)`
3. 客户端在重试请求中添加 USERNAME + REALM + NONCE + MESSAGE-INTEGRITY

## 8. 常见问题

### Q: Test II 超时
A: 检查 Coturn 是否启用了 `rfc5780` 选项，以及是否配置了两个 listening-ip。

### Q: TURN 401 Unauthorized 后无法分配
A: 检查：
1. 配置文件中有 `user=用户名:密码`
2. p2p_ping 的 `--turn-user` 和 `--turn-pass` 参数正确
3. Coturn 自动启用了 `lt-cred-mech`（查看启动日志）

### Q: TURN 分配成功但无法中继数据
A: 检查 CreatePermission 是否成功。TURN 需要先为对端 IP 创建权限才能转发数据。

### Q: 启动时提示 "Cannot create pid file"
A: 这是警告，不影响功能。Coturn 会在 `/var/tmp/` 创建 pid 文件。

### Q: 启动时提示 "Empty cli-password"
A: 这是警告，表示 CLI 管理接口被禁用，不影响 STUN/TURN 功能。

### Q: IP 别名丢失
A: macOS 重启后需重新添加：
```bash
sudo ifconfig en0 alias 10.2.120.149 netmask 255.255.255.0
```

## 9. 快速启动脚本

创建 `~/start_stun.sh`：

```bash
#!/bin/bash
# 添加 IP 别名（如果不存在）
if ! ifconfig en0 | grep -q "10.2.120.149"; then
    sudo ifconfig en0 alias 10.2.120.149 netmask 255.255.255.0
    echo "Added IP alias 10.2.120.149"
fi

# 停止已有实例
pkill turnserver 2>/dev/null

# 启动 Coturn
turnserver -c ~/turnserver.conf &
sleep 2

# 验证
if pgrep turnserver > /dev/null; then
    echo "Coturn started successfully"
    echo ""
    echo "=== STUN Test ==="
    stunclient 10.2.120.148
    echo ""
    echo "=== TURN Test ==="
    echo "Run: ./p2p_ping --stun 10.2.120.148 --turn 10.2.120.148 --turn-user test --turn-pass test123 -s localhost -n alice -l 5"
else
    echo "Failed to start Coturn"
fi
```

使用：
```bash
chmod +x ~/start_stun.sh
~/start_stun.sh
```

## 10. 测试命令速查

```bash
# 仅 STUN（NAT 检测）
./p2p_ping --stun 10.2.120.148 -s localhost -n alice -l 5

# STUN + TURN（完整 ICE 候选收集）
./p2p_ping \
  --stun 10.2.120.148 \
  --turn 10.2.120.148 \
  --turn-user test \
  --turn-pass test123 \
  -s localhost -n alice -l 5

# 仅 TURN（禁用 Host/Srflx，强制使用 Relay）
./p2p_ping \
  --turn 10.2.120.148 \
  --turn-user test \
  --turn-pass test123 \
  --no-host --no-srflx \
  -s localhost -n alice -l 5
```

## 参考

- [RFC 3489](https://tools.ietf.org/html/rfc3489) - STUN (Classic, with CHANGE-REQUEST)
- [RFC 5389](https://tools.ietf.org/html/rfc5389) - Session Traversal Utilities for NAT (STUN)
- [RFC 5780](https://tools.ietf.org/html/rfc5780) - NAT Behavior Discovery Using STUN
- [RFC 5766](https://tools.ietf.org/html/rfc5766) - Traversal Using Relays around NAT (TURN)
- [RFC 8656](https://tools.ietf.org/html/rfc8656) - TURN (Updated, obsoletes RFC 5766)
- [Coturn Documentation](https://github.com/coturn/coturn)
