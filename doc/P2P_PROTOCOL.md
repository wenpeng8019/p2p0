# P2P Signaling Server 协议文档

> **注意**: 此文档定义了服务器支持的两种信令模式协议。服务器参考实现见 `p2p_server/`。

## 概述

Signaling Server（信令服务器）负责：
1. **交换 Peer 信息** — 让两个 NAT 后的 peer 知道对方的地址/SDP
2. **中转信令/数据** — 转发连接请求、SDP 交换、Relay 数据

### 两种工作模式

| 模式 | 协议 | 传输层 | 用途 | 状态 |
|------|------|--------|------|------|
| **SIMPLE** | 自定义二进制 | UDP | 轻量级 NAT 穿透 | 无状态 |
| **ICE** | 自定义二进制 | TCP | 完整 ICE/SDP 交换 | 长连接 |

服务器可**同时支持两种模式**（同一端口，TCP + UDP）。

---

# 第一部分：COMPACT 模式（UDP）

## 数据包格式

所有包共享 4 字节头:

```
[ type: u8 | flags: u8 | seq: u16 ]  (network byte order)
```

## 核心概念：配对缓存机制

COMPACT 模式采用**双向配对缓存**设计，而非简单的单向映射。

### 配对记录结构

服务器为每个注册请求维护一条配对记录：

```c
typedef struct compact_pair_s {
    char local_peer_id[32];      // 本端 ID
    char remote_peer_id[32];     // 目标对端 ID
    struct sockaddr_in addr;     // 公网地址
    time_t last_seen;            // 最后活跃时间
    bool valid;                  // 记录是否有效
    struct compact_pair_s *peer;  // 指向配对的对端（NULL=未配对, 有效指针=已配对, -1=对方已断开）
} compact_pair_t;
```

### 配对匹配规则

当收到注册 `(A → B)` 时，服务器：
1. 查找本端记录：`local_peer_id=A AND remote_peer_id=B`
2. 查找反向记录：`local_peer_id=B AND remote_peer_id=A`
3. 如果找到反向记录（双向匹配），则**建立双向指针关系**

```
配对成功：
┌─────────────────┐           ┌─────────────────┐
│ local:  alice   │<── peer ──│ local:  bob     │
│ remote: bob     │── peer ──>│ remote: alice   │
└─────────────────┘           └─────────────────┘
```

### 为什么需要双向配对？

1. **对称性**：A 连接 B 和 B 连接 A 应该得到同样的结果
2. **快速查找**：通过 peer 指针 O(1) 访问对端信息
3. **状态追踪**：通过 peer 指针状态判断对方是否在线/断开
4. **地址变化检测**：已配对后地址变化可主动推送更新

## 流程详解

### 1. 注册与配对流程

```
Alice                  Server                    Bob
  |                      |                        |
  |--- REGISTER(bob) --->|                        |
  |   [alice → bob]      | [创建记录1: alice→bob]|
  |                      | [查找反向: bob→alice] |
  |                      | [未找到，等待]         |
  |                      |                        |
  |                      |<--- REGISTER(alice) ---|
  |                      |   [bob → alice]        |
  |                      | [创建记录2: bob→alice]|
  |                      | [查找反向: alice→bob] |
  |                      | [找到！建立双向指针]   |
  |                      |                        |
  |<-- PEER_INFO(bob) ---|--- PEER_INFO(alice) -->|
  |   [FIRST MATCH]      |   [BILATERAL]          |
  |   双向通知！          |                        |
```

#### COMPACT_PKT_REGISTER (0x01)

- **方向**: 客户端 → 服务器
- **Payload**: `[ local_peer_id: 32 bytes | remote_peer_id: 32 bytes ]` (64 bytes total)
- **说明**:
  - 客户端发送自己的 ID (`local_peer_id`) 和想要连接的对端 ID (`remote_peer_id`)
  - 服务器创建/更新配对记录 `(local_peer_id → remote_peer_id)`
  - 服务器查找反向配对 `(remote_peer_id → local_peer_id)`
  - 如果找到反向配对，建立双向指针，触发**双边通知**
  - 由于 UDP 不可靠，客户端会每 1 秒重发，直到收到 PEER_INFO

**为什么需要 local_peer_id?**

UDP 是无状态的，服务器无法识别"谁"发来的包。虽然有源地址（IP:Port），但服务器需要逻辑 ID 来：
1. 建立 `(local_peer_id, remote_peer_id) → 地址` 的配对映射
2. 允许客户端 IP 变化后重新注册（地址变化推送）
3. 支持多客户端在同一 NAT 后面（共享公网 IP）
4. 实现双向匹配逻辑

#### COMPACT_PKT_PEER_INFO (0x03)

- **方向**: 服务器 → 客户端
- **Payload**: `[ pub_ip: 4 | pub_port: 2 | priv_ip: 4 | priv_port: 2 ]` (12 bytes)
- **说明**:
  - 服务器在三种情况下发送此包
  - `pub_ip:pub_port` = 对方的公网地址（从 UDP 源地址获得）
  - `priv_ip:priv_port` = 对方的内网地址（目前使用公网地址，未来可由客户端上报）

**三种发送场景：**

| 场景 | 触发条件 | 发送对象 | 日志标记 |
|------|---------|---------|---------|
| **情况1：首次匹配** | 双向配对成功（`peer == NULL`） | **双方都发送** | `[FIRST MATCH]` + `[BILATERAL]` |
| **情况2：已配对重连** | 一方重新注册，已有配对 | 仅向请求方 | 无特殊标记 |
| **情况3：地址变化** | 检测到地址变化 | 向对方推送 | `[ADDR_CHANGED]` |

**服务器实现逻辑**:
```c
// 伪代码
on_register(local_id, remote_id, from_addr) {
    // 1. 查找或创建本端记录
    local = find_or_create(local_id, remote_id);
    
    // 2. 检测地址是否变化
    addr_changed = (local.addr != from_addr);
    
    // 3. 更新本端记录
    local.addr = from_addr;
    local.last_seen = now();
    local.valid = true;
    
    // 4. 查找反向配对
    remote = find_pair(remote_id, local_id);
    
    if (remote) {
        // 5. 判断是否首次匹配
        first_match = (local.peer == NULL || remote.peer == NULL);
        
        if (first_match) {
            // 建立双向指针
            local.peer = remote;
            remote.peer = local;
        }
        
        // 6. 情况2：向请求方发送对方地址（所有情况都需要）
        send_peer_info(from_addr, remote.addr);
        
        // 7. 情况1 或情况3：向对方推送本端地址
        if (first_match || (addr_changed && remote.peer == local)) {
            send_peer_info(remote.addr, from_addr);
        }
    }
}
```

### 2. 客户端地址更新处理

客户端收到 PEER_INFO 后，根据当前状态采取不同策略：

```c
on_peer_info(new_pub_addr, new_priv_addr) {
    addr_changed = (peer_pub_addr != new_pub_addr);
    peer_pub_addr = new_pub_addr;
    peer_priv_addr = new_priv_addr;
    
    switch (state) {
    case NAT_REGISTERING:
        // 首次收到对方地址 → 进入打洞阶段
        state = NAT_PUNCHING;
        start_punching();
        break;
        
    case NAT_PUNCHING:
        // 打洞中收到地址更新 → 重置打洞参数
        if (addr_changed) {
            reset_punch_params();
        }
        break;
        
    case NAT_CONNECTED:
        // 已连接收到地址更新 → 向新地址验证
        if (addr_changed) {
            send_punch(new_pub_addr);
        }
        break;
        
    case NAT_RELAY:
        // 中继模式收到地址更新 → 尝试直连
        if (addr_changed) {
            state = NAT_PUNCHING;
            start_punching();
        }
        break;
    }
}
```

### 3. 数据中转 (Relay)

当 NAT 打洞 5 秒仍失败时，客户端通过服务器中转。

#### RELAY_DATA (0x40)

- **方向**: 客户端 → 服务器 → 客户端
- **Payload**: `[ target_peer_id: 32 | original_data: N ]`
- **说明**:
  - 服务器收到后，剥离 `target_peer_id`，将 `original_data` 转发给目标 peer
  - 转发时保持包头 type=0x40，seq 保持不变

```
Alice                  Server                    Bob
  |                      |                        |
  |-- RELAY_DATA(bob) -->|-- RELAY_DATA(data) --->|
  |                      |                        |
  |<- RELAY_DATA(data) --|<- RELAY_DATA(alice) ---|
```

### 4. 超时与断开处理

服务器定期(10秒)清理过期记录：

```c
cleanup_compact_pairs() {
    for each pair in compact_pairs:
        if (pair.valid && now() - pair.last_seen > TIMEOUT) {
            // 如果有配对对端，标记对端的 peer 为 (void*)-1
            if (pair.peer != NULL && pair.peer != (void*)-1) {
                pair.peer.peer = (void*)-1;  // 对方知道自己已断开
            }
            pair.valid = false;
            pair.peer = NULL;
        }
}
```

客户端检测到 `peer == (void*)-1` 时：
- 重置为 `NULL`（允许重新配对）
- 重新发送 REGISTER 进入配对流程

### 5. 服务器实现要求

| 功能 | COMPACT 模式 (UDP) | RELAY 模式 (TCP) |
|------|------------------|----------------|
| 协议 | 无状态 UDP | 长连接 TCP |
| 数据结构 | `compact_pair_t` 双向配对 | `ice_client_t` 单向映射 |
| 超时清理 | 30 秒无 REGISTER 则清除 | 连接断开立即清除 |
| 地址变化 | 主动推送 PEER_INFO | N/A（TCP 连接地址不变） |
| 并发支持 | 需支持多对 peer 同时在线 | 同左 |
| 认证 | 无（基于 local_peer_id 信任） | 无（基于 local_peer_id 信任） |
| 中转限流 | RELAY 模式应限速 | 同左 |

**参考实现**: `p2p_server/server.c` 同时支持 TCP 和 UDP。

### 6. 完整流程示例（含地址变化）

```
Alice (local_peer_id="alice")    Server                Bob (local_peer_id="bob")
      |                      |                        |
      |-- REGISTER ---------->|                        |
      |  ["alice", "bob"]     |                        |
      |                       | [创建记录1]            |
      |                       | [查找反向: 未找到]     |
      |                       |                        |
      |                       |<-------- REGISTER -----|
      |                       |        ["bob", "alice"]|
      |                       | [创建记录2]            |
      |                       | [查找反向: 找到！]     |
      |                       | [建立双向指针]         |
      |                       |                        |
      |<--- PEER_INFO --------|-------- PEER_INFO ---->|
      | [FIRST MATCH]         |     [BILATERAL]        |
      |                       |                        |
      | NAT 打洞阶段 ------------------------------------|
      |<---------------- PUNCH / PUNCH_ACK ------------>|
      | [成功建立 P2P 连接]    |                        |
      |                       |                        |
      | P2P 直连数据传输 ---------------------------------|
      |<==================== DATA ====================>|
      |                       |                        |
      | [Alice IP 变化] ------>|                        |
      |-- REGISTER ---------->|                        |
      |  ["alice", "bob"]     |                        |
      |                       | [检测到地址变化]       |
      |                       | [更新记录1]            |
      |                       |                        |
      |<--- PEER_INFO --------|-------- PEER_INFO ---->|
      |   [地址确认]          |     [ADDR_CHANGED]     |
      |                       |    [主动推送！]        |
      |                       |                        |
      |<-------- 重新打洞，恢复直连 ------------------->|
```

### 关键时间参数

- `REGISTER_INTERVAL_MS = 1000ms` — 注册重发间隔
- `PUNCH_INTERVAL_MS = 500ms` — 打洞间隔
- `PUNCH_TIMEOUT_MS = 5000ms` — 打洞超时（转 RELAY）
- `PING_INTERVAL_MS = 15000ms` — 心跳间隔
- `PONG_TIMEOUT_MS = 30000ms` — 心跳超时
- `COMPACT_PAIR_TIMEOUT = 30s` — 服务器配对记录超时

---

# 第二部分：RELAY 模式（TCP）

## 概述

RELAY 模式使用 **TCP 长连接**进行信令交换，支持完整的 ICE/SDP 协商流程。相比 COMPACT 模式：
- ✅ 可靠传输（TCP 自动重传）
- ✅ 支持大型 SDP 负载（WebRTC 场景）
- ✅ 服务器可主动推送
- ⚠️ 需要维护连接状态
- ⚠️ 需要心跳检测死连接

## 协议格式

所有消息共享统一的二进制包头：

```
[ magic: u32 | type: u8 | length: u32 ]  (9 bytes, network byte order)
```

- `magic = 0x50325030` ("P2P0")
- `type` = 消息类型（见下表）
- `length` = 后续 Payload 长度（字节）

### 消息类型

| 值 | 名称 | 方向 | Payload | 说明 |
|----|------|------|---------|------|
| 1 | P2P_RLY_LOGIN | C→S | `name[32]` | 客户端登录 |
| 2 | P2P_RLY_LOGIN_ACK | S→C | 无 | 登录确认 |
| 3 | P2P_RLY_LIST | C→S | 无 | 请求在线用户列表 |
| 4 | P2P_RLY_LIST_RES | S→C | `"alice,bob,..."` | 返回用户列表（逗号分隔） |
| 5 | P2P_RLY_CONNECT | C→S | `target[32] + SDP[N]` | 发起连接请求 |
| 6 | P2P_RLY_OFFER | S→C | `from[32] + SDP[N]` | 转发连接请求 |
| 7 | P2P_RLY_ANSWER | C→S | `target[32] + SDP[N]` | 返回应答 |
| 8 | P2P_RLY_FORWARD | S→C | `from[32] + SDP[N]` | 转发应答 |
| 9 | P2P_RLY_HEARTBEAT | C→S | 无 | 心跳（保持连接） |

**注**：C = Client, S = Server

## 工作流程

### 1. 客户端登录

```
Client                    Server
  |                          |
  |--- P2P_RLY_LOGIN ----------->|
  |    name="alice"          |
  |                          | [记录客户端: fd→name]
  |<-- P2P_RLY_LOGIN_ACK --------|
  |                          |
```

**P2P_RLY_LOGIN 结构**：
```
[ Header: 9 bytes ]
[ name: 32 bytes ]  // 客户端名称（用于寻址）
```

服务器响应：
- 成功：发送 `P2P_RLY_LOGIN_ACK`，记录 `(fd → name)` 映射
- 失败：关闭连接

### 2. 查询在线用户

```
Client                    Server
  |                          |
  |--- P2P_RLY_LIST ------------>|
  |                          | [遍历在线客户端]
  |<-- P2P_RLY_LIST_RES ---------|
  |    "bob,charlie,..."     |
```

**P2P_RLY_LIST_RES 结构**：
```
[ Header: 9 bytes ]
[ list: N bytes ]  // "bob,charlie,david," (逗号分隔)
```

### 3. ICE/SDP 交换（完整流程）

```
Alice                   Server                    Bob
  |                        |                        |
  |--- P2P_RLY_CONNECT ------->|                        |
  |  target="bob"          |                        |
  |  SDP(offer)            |                        |
  |                        |--- P2P_RLY_OFFER -------->|
  |                        |  from="alice"          |
  |                        |  SDP(offer)            |
  |                        |                        |
  |                        |<-- P2P_RLY_ANSWER -----|
  |                        |  target="alice"        |
  |                        |  SDP(answer)           |
  |<-- P2P_RLY_FORWARD ---|                        |
  |  from="bob"            |                        |
  |  SDP(answer)           |                        |
  |                        |                        |
  |<========== ICE 协商，建立 P2P 连接 =============>|
```

**关键点**：
1. Alice 发送 `P2P_RLY_CONNECT` → 服务器转换为 `P2P_RLY_OFFER` 发给 Bob
2. Bob 发送 `P2P_RLY_ANSWER` → 服务器转换为 `P2P_RLY_FORWARD` 发给 Alice
3. **服务器不解析 SDP 内容**，仅作透明转发
4. **转发时更换消息类型**（见消息转发规则）

#### 消息转发规则

| 收到消息 | 转发为 | 原因 |
|---------|--------|------|
| P2P_RLY_CONNECT | P2P_RLY_OFFER | 通知对方"有人要连接你" |
| P2P_RLY_ANSWER | P2P_RLY_FORWARD | 转发应答给发起方 |

**包结构示例（P2P_RLY_CONNECT）**：
```
[ Header: magic=0x50325030, type=5, length=32+N ]
[ target_name: 32 bytes ]  // 目标客户端名称
[ sdp_data: N bytes ]      // SDP offer/answer 数据
```

### 4. 心跳与死连接检测

#### 为什么需要应用层心跳？

TCP 连接在以下情况**无法检测断开**：
- ❌ 客户端进程崩溃（无 FIN 包）
- ❌ 网络中断（无 RST 包）
- ❌ NAT 超时（中间设备关闭映射）

服务器会**永远等待**，造成"僵尸连接"占用资源。

#### 心跳机制

```
Client                    Server
  |                          |
  |--- P2P_RLY_HEARTBEAT ------->|
  | (每 30 秒)               | [更新 last_active]
  |                          |
  |    (如果 60 秒无任何消息) |
  |                          | [检测超时]
  |                      X---|  [关闭连接]
```

**实现细节**：
```c
#define ICE_CLIENT_TIMEOUT 60  // 秒

typedef struct {
    int fd;
    char name[32];
    time_t last_active;  // 最后活跃时间
    bool valid;
} ice_client_t;

// 收到任何消息时更新
on_recv_message(client_idx) {
    g_ice_clients[client_idx].last_active = time(NULL);
}

// 定期清理（每 10 秒）
cleanup_ice_clients() {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (valid && (now - last_active) > ICE_CLIENT_TIMEOUT) {
            close(fd);
            valid = false;
        }
    }
}
```

**客户端建议**：
- 每 30 秒发送一次 `P2P_RLY_HEARTBEAT`
- 服务器 60 秒超时，留出 2 倍余量
- 也可通过其他消息（LIST、CONNECT 等）刷新活跃时间

### 5. 错误处理

| 错误场景 | 服务器行为 |
|---------|----------|
| 收到无效 magic | 关闭连接 |
| 目标用户不在线 | 丢弃消息，打印日志 |
| Payload 过大 (>65536) | 关闭连接 |
| recv 失败 | 关闭连接，标记 valid=false |
| 客户端超时 | 主动关闭连接 |

### 6. 服务端实现

#### 关键数据结构

```c
typedef struct {
    int fd;                  // TCP socket
    char name[P2P_PEER_ID_MAX]; // 客户端名称
    time_t last_active;      // 最后活跃时间
    bool valid;              // 是否有效
} ice_client_t;

static ice_client_t g_ice_clients[MAX_PEERS];
```

#### 主循环逻辑

```c
while (1) {
    // 1. 定期清理超时连接
    if (now - last_cleanup >= 10) {
        cleanup_ice_clients();
    }
    
    // 2. select 监听所有 fd
    FD_ZERO(&read_fds);
    FD_SET(listen_fd, &read_fds);  // TCP 监听
    FD_SET(udp_fd, &read_fds);     // UDP (COMPACT 模式)
    for (client in ice_clients) {
        FD_SET(client.fd, &read_fds);
    }
    
    select(max_fd + 1, &read_fds, ...);
    
    // 3. 处理新连接
    if (FD_ISSET(listen_fd, &read_fds)) {
        client_fd = accept(...);
        // 添加到 ice_clients[]
    }
    
    // 4. 处理客户端消息
    for (client in ice_clients) {
        if (FD_ISSET(client.fd, &read_fds)) {
            handle_ice_signaling(client_idx);
        }
    }
}
```

## ICE vs SIMPLE 对比

| 特性 | COMPACT 模式 | RELAY 模式 |
|------|------------|----------|
| **传输层** | UDP | TCP |
| **连接状态** | 无状态 | 长连接 |
| **重传** | 客户端负责 | TCP 自动 |
| **负载大小** | < 1500 bytes | 无限制（建议 < 64KB） |
| **服务器清理** | 30秒无 REGISTER | 60秒无消息 |
| **地址变化** | 主动推送 | N/A（TCP 地址不变） |
| **死连接检测** | N/A（UDP 无连接） | 应用层心跳 |
| **用户寻址** | local_peer_id (32 bytes) | name (32 bytes) |
| **典型场景** | 嵌入式/IoT | WebRTC/跨平台 |
| **消息格式** | `[type:u8|flags:u8|seq:u16]` | `[magic:u32|type:u8|length:u32]` |

## 完整示例：WebRTC 连接建立

```
Alice (浏览器)          Server              Bob (浏览器)
     |                     |                      |
     |--- P2P_RLY_LOGIN ------>|                      |
     |   name="alice"      |                      |
     |<-- P2P_RLY_LOGIN_ACK ---|                      |
     |                     |<---- P2P_RLY_LOGIN ------|
     |                     |     name="bob"       |
     |                     |--- P2P_RLY_LOGIN_ACK --->|
     |                     |                      |
     |--- P2P_RLY_LIST ------->|                      |
     |<-- P2P_RLY_LIST_RES ----|                      |
     |   "bob"             |                      |
     |                     |                      |
     | [创建 RTCPeerConnection, 生成 offer]       |
     |                     |                      |
     |--- P2P_RLY_CONNECT ---->|                      |
     |  target="bob"       |                      |
     |  SDP: {type:"offer"}|                      |
     |                     |--- P2P_RLY_OFFER ------>|
     |                     |  from="alice"        |
     |                     |  SDP: {type:"offer"} |
     |                     |                      |
     |                     | [创建 answer]        |
     |                     |                      |
     |                     |<-- P2P_RLY_ANSWER ---|
     |                     |  target="alice"      |
     |                     |  SDP: {type:"answer"}|
     |<-- P2P_RLY_FORWARD-|                      |
     |  from="bob"         |                      |
     |  SDP: {type:"answer"}                     |
     |                     |                      |
     | [设置 remote description]                  |
     |<===== ICE candidates 交换 =================>|
     | [STUN/TURN 打洞]                           |
     |<===== 建立 P2P 连接 =======================>|
     |                     |                      |
     |--- P2P_RLY_HEARTBEAT -->|<-- P2P_RLY_HEARTBEAT ----|
     | (每 30 秒)          | (保持连接)           |
```

## 附录：完整包类型列表

### COMPACT 模式（UDP）

| 范围 | 分类 | 包类型 |
|------|------|--------|
| **0x01-0x0F** | **信令协议** | |
| 0x01 | P2P_PKT_REGISTER | 注册配对请求 |
| 0x02 | P2P_PKT_REGISTER_ACK | 注册确认 |
| 0x03 | P2P_PKT_PEER_INFO | 对端地址通知 |
| **0x10-0x1F** | **打洞协议** | |
| 0x10 | P2P_PKT_PUNCH | NAT 打洞 |
| 0x11 | P2P_PKT_PUNCH_ACK | 打洞确认 |
| **0x20-0x2F** | **保活协议** | |
| 0x20 | P2P_PKT_PING | 心跳请求 |
| 0x21 | P2P_PKT_PONG | 心跳响应 |
| **0x30-0x3F** | **数据传输** | |
| 0x30 | P2P_PKT_DATA | 应用数据 |
| 0x31 | P2P_PKT_ACK | 数据确认 |
| 0x32 | P2P_PKT_FIN | 连接关闭 |
| **0x40-0x4F** | **中继协议** | |
| 0x40 | P2P_PKT_RELAY_DATA | 服务器中转 |
| **0x50-0x5F** | **路由探测** | |
| 0x50 | P2P_PKT_ROUTE_PROBE | 路由探测 |
| 0x51 | P2P_PKT_ROUTE_PROBE_ACK | 路由探测确认 |
| **0x60-0x6F** | **安全协议** | |
| 0x60 | P2P_PKT_AUTH | 安全握手 |

### RELAY 模式（TCP）

| 值 | 名称 | 说明 |
|----|------|------|
| 1 | P2P_RLY_LOGIN | 登录 |
| 2 | P2P_RLY_LOGIN_ACK | 登录确认 |
| 3 | P2P_RLY_LIST | 查询在线用户 |
| 4 | P2P_RLY_LIST_RES | 用户列表响应 |
| 5 | P2P_RLY_CONNECT | 发起连接 |
| 6 | P2P_RLY_OFFER | 转发连接请求 |
| 7 | P2P_RLY_ANSWER | 返回应答 |
| 8 | P2P_RLY_FORWARD | 转发应答 |
| 9 | P2P_RLY_HEARTBEAT | 心跳 |

## 安全注意事项

> [!CAUTION]
> 当前协议设计侧重功能完整性，**不含任何加密或认证机制**。

### 通用安全建议

| 威胁 | 解决方案 |
|------|---------|
| **窃听** | DTLS/TLS 加密（库已支持，见 `use_dtls` 配置） |
| **身份冒用** | 添加认证 token、签名机制 |
| **重放攻击** | 添加 nonce/timestamp 验证 |
| **DDoS** | Rate limiting、IP 白名单 |
| **中间人攻击** | 端到端加密、证书验证 |

### COMPACT 模式特定风险

- ❌ **无 local_peer_id 验证**：任何人可冒充他人注册
- ❌ **明文传输**：地址信息未加密
- ⚠️ **UDP 泛洪**：需要服务器端 rate limiting

### RELAY 模式特定风险

- ❌ **无登录验证**：任何人可连接服务器
- ❌ **SDP 劫持**：中间人可修改 ICE candidates
- ⚠️ **死连接占用**：已通过心跳超时机制解决 ✅

### 生产部署 Checklist

- [ ] 启用 DTLS/TLS 加密
- [ ] 实现 token 认证机制
- [ ] 配置防火墙规则
- [ ] 部署 rate limiting（建议：100 req/min/IP）
- [ ] 监控异常连接模式
- [ ] 定期更新依赖库（mbedtls/openssl）
- [ ] 记录审计日志

---

**参考实现**：`p2p_server/server.c`

**测试覆盖**：43/43 单元测试通过
- 14 传输层测试
- 10 COMPACT 模式测试  
- 19 ICE 服务端测试

**最后更新**：2026-02-15
