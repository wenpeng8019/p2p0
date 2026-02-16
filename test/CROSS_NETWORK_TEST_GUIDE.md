# 跨网络 P2P 测试指南

**目的：** 在真实 NAT 环境下验证 P2P 库的 NAT 穿透能力

---

## 测试场景设计

### 场景 A: 家庭网络 ↔ 办公室网络

这是最典型的跨网络测试场景，用于验证：
- ✅ 真实 NAT 穿透
- ✅ STUN 公网映射
- ✅ UDP 打洞成功率
- ✅ 端口分配随机性

**网络拓扑：**
```
┌─────────────────────┐              ┌─────────────────────┐
│   家庭网络           │              │   办公室网络         │
│   (NAT1)            │              │   (NAT2)            │
│                     │              │                     │
│  Peer Alice         │              │  Peer Bob           │
│  10.0.0.50          │              │  192.168.1.100      │
│  私网端口: 随机      │              │  私网端口: 随机      │
└─────────┬───────────┘              └─────────┬───────────┘
          │ NAT转换                            │ NAT转换
          │ 公网IP1:端口A                      │ 公网IP2:端口B
          │                                    │
          └────────────────┬───────────────────┘
                           │
                     Internet
                           │
               ┌───────────┴──────────┐
               │  信令服务器           │
               │  (公网部署)          │
               │  或 GitHub Gist     │
               └─────────────────────┘
```

---

## 准备工作

### 1. 部署信令服务器（公网）

**选项 A: 使用云服务器**

在 AWS/阿里云/腾讯云等平台启动一台服务器：

```bash
# 登录云服务器
ssh user@your-server-ip

# 上传编译好的 p2p_server
scp build_cmake/p2p_server/p2p_server user@your-server-ip:~/

# 在服务器上运行
./p2p_server 8888

# 保持运行（使用 screen 或 tmux）
screen -S p2p_server
./p2p_server 8888
# Ctrl+A, D 离开但保持运行
```

**安全配置：**
```bash
# 开放端口 8888（TCP）
# AWS Security Group / 阿里云安全组
# 允许入站规则: TCP 8888 from 0.0.0.0/0
```

**选项 B: 使用 GitHub Gist 信令**

完全无需服务器，参见步骤 3。

---

### 2. 配置 STUN 服务器

**使用公共 STUN 服务器（推荐）：**

代码中已默认使用 `stun.l.google.com:3478`，无需额外配置。

**备用 STUN 服务器列表：**
```
stun.l.google.com:3478
stun1.l.google.com:3478
stun2.l.google.com:3478
stun.stunprotocol.org:3478
```

**如需更换，修改代码：**
```c
// 在 p2p_stun.c 或 p2p_ice.c 中
strncpy(cfg->stun_server, "stun.stunprotocol.org", 63);
cfg->stun_port = 3478;
```

---

## 测试执行步骤

### 测试 A: 信令服务器模式（推荐）

#### 在家庭网络（设备 A）运行 Alice：

```bash
cd /path/to/p2p

# 启动 Alice（订阅者）
./build_cmake/p2p_ping/p2p_ping \
    --name alice \
    --server YOUR_SERVER_IP:8888 \
    2>&1 | tee alice_cross_network.log
```

**预期看到：**
```
=== P2P Ping Diagnostic Tool ===
[ICE] Gathered Host Candidate: 10.0.0.50:xxxxx
[ICE] Gathered Host Candidate: 192.168.x.x:xxxxx (如有多网卡)
[NAT] Sending STUN Test I to stun.l.google.com...
[NAT] Received STUN Response from 74.125.250.129:3478
[ICE] Gathered Srflx Candidate: <家庭公网IP>:<映射端口>
[NAT] Result: FULL_CONE / RESTRICTED / PORT_RESTRICTED / SYMMETRIC
Signaling: Connected to server YOUR_SERVER_IP:8888 as 'alice'
Running in SERVER mode...
[STATE] UNKNOWN (-1) -> IDLE (0)
```

#### 在办公室网络（设备 B）运行 Bob：

```bash
cd /path/to/p2p

# 启动 Bob（发布者，连接到 Alice）
./build_cmake/p2p_ping/p2p_ping \
    --name bob \
    --server YOUR_SERVER_IP:8888 \
    --to alice \
    2>&1 | tee bob_cross_network.log
```

**预期看到：**
```
=== P2P Ping Diagnostic Tool ===
[ICE] Gathered Host Candidate: 192.168.1.100:xxxxx
[NAT] Sending STUN Test I to stun.l.google.com...
[ICE] Gathered Srflx Candidate: <办公室公网IP>:<映射端口>
Signaling: Connected to server YOUR_SERVER_IP:8888 as 'bob'
Signaling [PUB]: Sent connect request to 'alice' (396 bytes)
[STATE] UNKNOWN (-1) -> IDLE (0)
Signaling: Received signal from 'alice' (396 bytes)
[ICE] Handling signaling payload from 'alice' (8 candidates)
[ICE] Added Remote Candidate: 0 -> 10.0.0.50:xxxxx
[ICE] Added Remote Candidate: 1 -> <家庭公网IP>:<映射端口>
[ICE] Sending connectivity check to Candidate 0: ...
[ICE] Sending connectivity check to Candidate 1: ...
```

#### 成功标志：

**Alice 端：**
```
[ICE] Nomination successful! Using path <公网IP>:<端口>
[STATE] IDLE (0) -> CONNECTED (3)
[DATA] Sent PING
[DATA] Received: P2P_PING_ALIVE
```

**Bob 端：**
```
[ICE] Nomination successful! Using path <公网IP>:<端口>
[STATE] IDLE (0) -> CONNECTED (3)
[DATA] Sent PING
[DATA] Received: P2P_PING_ALIVE
```

**服务器端日志：**
```
Peer 'alice' logged in (fd: 4)
Peer 'bob' logged in (fd: 5)
Relaying signal from bob to alice (396 bytes)
Relaying signal from alice to bob (396 bytes)
Peer alice disconnected
Peer bob disconnected
```

---

### 测试 B: GitHub Gist 信令模式（无需服务器）

#### 准备工作：

1. **创建 GitHub Personal Access Token**
   ```
   访问: https://github.com/settings/tokens
   点击: Generate new token (classic)
   权限: 勾选 'gist' 选项
   生成并复制 token: ghp_xxxxxxxxxxxxxxxxxxxx
   ```

2. **创建空白 Gist**
   ```
   访问: https://gist.github.com/
   创建新 Gist:
     文件名: p2p_signaling.txt
     内容: (留空)
     可见性: Secret (推荐) 或 Public
   创建后复制 Gist ID (URL 最后一段)
   例如: https://gist.github.com/username/abc123def456
       => Gist ID: abc123def456
   ```

3. **设置环境变量**
   ```bash
   export P2P_GITHUB_TOKEN="ghp_xxxxxxxxxxxxxxxxxxxx"
   export P2P_GIST_ID="abc123def456"
   ```

#### 在设备 A（Alice - 订阅者）：

```bash
./build_cmake/p2p_ping/p2p_ping \
    --name alice \
    --github \
    --gist $P2P_GIST_ID \
    2>&1 | tee alice_gist.log
```

**预期看到：**
```
[ICE] Gathered candidates...
Running in GIST mode...
[SIGNAL_PUB] Role: SUBSCRIBER
[SIGNAL_PUB] Polling Gist...
```

#### 在设备 B（Bob - 发布者）：

```bash
./build_cmake/p2p_ping/p2p_ping \
    --name bob \
    --github \
    --gist $P2P_GIST_ID \
    --to alice \
    2>&1 | tee bob_gist.log
```

**预期看到：**
```
[SIGNAL_PUB] Role: PUBLISHER
[SIGNAL_PUB] Publishing offer to Gist...
[SIGNAL_PUB] Polling for answer...
```

**Gist 内容示例：**
```json
{
  "offer": "<base64编码的加密SDP>",
  "answer": "<base64编码的加密SDP>"
}
```

---

## NAT 类型与测试矩阵

### NAT 类型检测结果

你的库会自动检测 NAT 类型：

| NAT 类型 | 特征 | P2P 可行性 |
|---------|------|----------|
| **Full Cone** | 任意外部主机可以连接 | ✅ 最容易 |
| **Restricted Cone** | 需先向外发包 | ✅ 容易 |
| **Port Restricted** | 端口也需匹配 | ✅ 中等 |
| **Symmetric** | 每个目标不同映射 | ⚠️ 困难 |
| **BLOCKED** | 防火墙阻止 UDP | ❌ 需 TURN |

### 测试组合矩阵

| Alice NAT | Bob NAT | 预期结果 | 备注 |
|-----------|---------|---------|------|
| Full Cone | Full Cone | ✅ 直连 | 理想情况 |
| Full Cone | Restricted | ✅ 直连 | Alice 无限制 |
| Restricted | Restricted | ✅ 打洞成功 | 典型场景 |
| Restricted | Port Restricted | ✅ 打洞成功 | 稍慢 |
| Symmetric | Restricted | ⚠️ 可能失败 | 端口预测 |
| Symmetric | Symmetric | ❌ 失败 | 需 TURN 中继 |

---

## 故障排查

### 问题 1: 连接卡在 PUNCHING 状态

**日志特征：**
```
[STATE] IDLE (0) -> PUNCHING (2)
[ICE] Sending connectivity check to Candidate 0: ...
[ICE] Sending connectivity check to Candidate 1: ...
(重复但无 CONNECTED)
```

**可能原因：**
1. **两端都是 Symmetric NAT**
   - 解决：配置 TURN 服务器中继
   
2. **防火墙阻止 UDP**
   - 检查：`sudo ufw status` (Linux) 或防火墙设置
   - 解决：允许 UDP 出站/入站
   
3. **STUN 服务器不可达**
   - 测试：`nc -u -v stun.l.google.com 3478`
   - 解决：更换 STUN 服务器

4. **信令交换不完整**
   - 检查服务器日志是否显示 "Relaying signal"
   - 检查 Gist 是否包含 offer 和 answer

### 问题 2: NAT 类型检测失败

**日志特征：**
```
[NAT] Sending STUN Test I to stun.l.google.com...
[NAT] Timeout waiting for STUN response
[NAT] Result: UNKNOWN
```

**解决：**
```bash
# 测试网络连接性
ping 8.8.8.8

# 测试 DNS 解析
nslookup stun.l.google.com

# 测试 UDP 连通性
nc -u -v 74.125.250.129 3478

# 检查是否有代理/VPN 干扰
env | grep -i proxy
```

### 问题 3: 信令服务器连接失败

**日志特征：**
```
Signaling: Failed to connect to server YOUR_SERVER_IP:8888
```

**解决：**
```bash
# 测试服务器可达性
telnet YOUR_SERVER_IP 8888

# 检查服务器是否在运行
ssh user@YOUR_SERVER_IP 'ps aux | grep p2p_server'

# 检查服务器防火墙
ssh user@YOUR_SERVER_IP 'sudo iptables -L -n | grep 8888'
# 或
ssh user@YOUR_SERVER_IP 'sudo ufw status'
```

### 问题 4: GitHub Gist 信令超时

**日志特征：**
```
[SIGNAL_PUB] HTTP request failed: 401 Unauthorized
```

**解决：**
```bash
# 验证 Token
curl -H "Authorization: token $P2P_GITHUB_TOKEN" \
     https://api.github.com/user

# 验证 Token 权限
curl -H "Authorization: token $P2P_GITHUB_TOKEN" \
     https://api.github.com/gists/$P2P_GIST_ID

# 重新生成 Token（确保勾选 'gist' 权限）
```

---

## 性能测量

### 连接建立时间

在日志中查找时间戳：

```bash
# Alice 日志
grep "\[STATE\].*IDLE.*CONNECTED" alice_cross_network.log
# 例如: 16:23:45.123 [STATE] IDLE (0) -> CONNECTED (3)

# Bob 日志
grep "\[STATE\].*IDLE.*CONNECTED" bob_cross_network.log
# 例如: 16:23:45.234 [STATE] IDLE (0) -> CONNECTED (3)

# 计算差值：约 0.111 秒
```

**正常范围：**
- 本地网络: 1-3 秒
- 跨网直连: 3-8 秒
- 需多次 ICE 检查: 8-15 秒
- TURN 中继: 5-12 秒

### 带宽测试（可选）

修改 `p2p_ping.c` 发送更多数据：

```c
// 在 CONNECTED 状态后
if (p2p_is_ready(s)) {
    char large_data[1024] = {0};
    memset(large_data, 'X', sizeof(large_data));
    
    for (int i = 0; i < 1000; i++) {
        p2p_send(s, large_data, sizeof(large_data));
        usleep(10000); // 10ms
    }
    // 发送 1MB 数据，测量时间
}
```

---

## 数据收集

### 需要记录的信息

**Alice 端：**
- [ ] 操作系统和版本
- [ ] 网络类型（家庭/公司/移动网络）
- [ ] NAT 类型检测结果
- [ ] 本地 IP 和端口
- [ ] 公网 IP 和映射端口（从 STUN）
- [ ] 连接建立耗时
- [ ] 最终选择的路径（Host/Srflx/Relay）

**Bob 端：**
- 同上

**网络环境：**
- [ ] 两端之间的物理距离
- [ ] 是否在同一 ISP
- [ ] 是否使用 VPN
- [ ] 网络延迟（ping 对方公网 IP）

### 测试报告模板

```markdown
## 跨网络测试结果

**测试时间：** YYYY-MM-DD HH:MM

### 网络信息
- **Alice 位置：** 北京家庭网络
- **Alice NAT：** Port Restricted Cone
- **Alice 公网 IP：** 123.45.67.89:12345
- **Bob 位置：** 上海办公网络
- **Bob NAT：** Restricted Cone
- **Bob 公网 IP：** 98.76.54.32:54321

### 测试结果
- **信令方式：** 云服务器 (aliyun.example.com:8888)
- **连接状态：** ✅ 成功
- **连接耗时：** 4.2 秒
- **选择路径：** Srflx (公网到公网)
- **数据传输：** ✅ PING/PONG 正常
- **NAT 穿透方式：** UDP 打洞

### 日志文件
- alice_cross_network.log
- bob_cross_network.log
```

---

## 高级测试场景

### 场景 1: 移动网络 (4G/5G) ↔ Wi-Fi

**特点：**
- 移动网络通常是 Symmetric NAT
- IP 地址可能频繁变化
- 需要测试 ICE restart 机制

**测试步骤：**
```bash
# 在手机（需安装 Termux 或交叉编译）
./p2p_ping --name mobile --server SERVER:8888

# 在 PC
./p2p_ping --name pc --server SERVER:8888 --to mobile
```

### 场景 2: VPN 环境测试

**测试 VPN 对 NAT 穿透的影响：**

```bash
# Alice 使用 VPN
# 连接 VPN 后运行
./p2p_ping --name alice_vpn --server SERVER:8888

# Bob 不使用 VPN
./p2p_ping --name bob --server SERVER:8888 --to alice_vpn
```

### 场景 3: 多跳 NAT（双重 NAT）

**拓扑：**
```
设备 (10.0.1.100) 
  ↓
路由器1 (10.0.1.1 / 192.168.0.100) 
  ↓
路由器2 (192.168.0.1 / 公网IP)
  ↓
Internet
```

**预期：** STUN 只能看到最外层 NAT 的映射

---

## 自动化测试脚本

### cross_network_test.sh

```bash
#!/bin/bash

SERVER="${1:-your-server.com:8888}"
LOG_DIR="cross_network_logs_$(date +%Y%m%d_%H%M%S)"

mkdir -p "$LOG_DIR"

echo "🌐 跨网络 P2P 测试"
echo "===================="
echo "信令服务器: $SERVER"
echo "日志目录: $LOG_DIR"
echo ""

# 检测本地网络环境
echo "📡 检测本地网络..."
LOCAL_IP=$(ifconfig | grep 'inet ' | grep -v 127.0.0.1 | head -1 | awk '{print $2}')
echo "本地 IP: $LOCAL_IP"

# 询问角色
read -p "选择角色 (alice/bob): " ROLE
read -p "对方名称 (如果是 Bob，输入 Alice 的名字): " TARGET

if [ "$ROLE" = "alice" ]; then
    echo "🟢 启动 Alice (订阅者)..."
    ./build_cmake/p2p_ping/p2p_ping \
        --name alice \
        --server "$SERVER" \
        2>&1 | tee "$LOG_DIR/alice.log"
else
    echo "🔵 启动 Bob (发布者)..."
    ./build_cmake/p2p_ping/p2p_ping \
        --name bob \
        --server "$SERVER" \
        --to "$TARGET" \
        2>&1 | tee "$LOG_DIR/bob.log"
fi
```

**使用方法：**
```bash
chmod +x cross_network_test.sh

# 在设备 A
./cross_network_test.sh your-server.com:8888
# 输入: alice

# 在设备 B
./cross_network_test.sh your-server.com:8888
# 输入: bob
# 输入: alice
```

---

## 预期结果与成功标准

### ✅ 测试成功标准

1. **ICE 候选收集完成**
   - 至少 2 个候选（Host + Srflx）
   - NAT 类型检测完成

2. **信令交换成功**
   - 双方都收到对方的 SDP
   - 服务器日志显示 "Relaying signal"

3. **连接建立**
   - 状态转换到 CONNECTED (3)
   - 日志显示 "Nomination successful"

4. **数据传输**
   - 收到 "P2P_PING_ALIVE"
   - 无 packet loss

5. **性能指标**
   - 连接时间 < 15 秒
   - 选择最优路径（优先 Srflx）

---

## 总结

完成跨网络测试后，你将验证：

✅ **真实 NAT 穿透能力** - 不同网络环境的连通性  
✅ **STUN 集成有效性** - 公网映射获取  
✅ **ICE 协商健壮性** - 候选选择和连接建立  
✅ **信令机制可靠性** - 服务器或 Gist 模式  
✅ **生产环境适用性** - 真实场景表现

这是 P2P 库最重要的测试环节！

---

**下一步：** GitHub Gist 信令详细测试（见 [GIST_DEPLOYMENT_GUIDE.md](GIST_DEPLOYMENT_GUIDE.md) 和 [GIST_SIGNALING_MECHANISM.md](GIST_SIGNALING_MECHANISM.md)）
