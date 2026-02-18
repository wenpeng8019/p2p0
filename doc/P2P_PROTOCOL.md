# P2P 信令协议文档

> **定义**: 此文档定义了 P2P 系统支持的三种信令模式协议规范。  
> **参考实现**: `p2p_server/server.c` (服务器端), `src/p2p_signal_*.c` (客户端)  
> **协议定义**: `include/p2pp.h` (统一协议头文件)

---

## 概述

P2P 系统采用**信令-打洞-数据传输**三阶段架构，信令层负责：
1. **候选交换** — 让 NAT 后的双方互相知道对方的网络地址（ICE Candidates）
2. **状态同步** — 通知对端上下线、地址变化等事件
3. **可靠转发** — 在 P2P 打洞失败时提供降级数据中继

### 三种信令模式

| 模式 | 传输层 | 服务器 | 实时性 | 离线缓存 | 部署 | 典型场景 |
|------|--------|--------|--------|---------|------|---------|
| **COMPACT** | UDP | 自建 | 高 | 整包 | 需自建 | 嵌入式/移动网络 |
| **RELAY** | TCP | 自建 | 高 | 单候选 | 需自建 | 企业内网/WebRTC |
| **PUBSUB** | HTTPS | GitHub Gist | 低 | Gist 持久化 | 零部署 | 快速原型/演示 |

**服务器兼容性**：`p2p_server` 可同时支持 COMPACT (UDP端口) 和 RELAY (TCP端口)。

---

## 通用包头定义

所有 P2P 协议包共享 **4 字节包头**（网络字节序）：

```c
typedef struct {
    uint8_t  type;    // 包类型 (0x01-0x7F: P2P协议, 0x80-0xFF: 信令协议)
    uint8_t  flags;   // 标志位（具体含义由 type 决定）
    uint16_t seq;     // 序列号（用于 ACK、去重、顺序控制）
} p2p_packet_hdr_t;
```

**编解码辅助函数**：
```c
void p2p_pkt_hdr_encode(uint8_t *buf, uint8_t type, uint8_t flags, uint16_t seq);
void p2p_pkt_hdr_decode(const uint8_t *buf, p2p_packet_hdr_t *hdr);
```

---

# 第一部分：COMPACT 模式 (UDP)

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

# 第二部分：RELAY 模式 (TCP)

## 概述

RELAY 模式使用 **TCP 长连接**进行信令交换，支持完整的 ICE/Trickle ICE 候选协商流程。

### 核心特性

- ✅ **可靠传输**：TCP 自动重传，无需应用层确认
- ✅ **实时推送**：服务器可主动推送消息（OFFER/FORWARD）
- ✅ **离线缓存**：支持单候选粒度缓存（最大 255 个/用户）
- ✅ **Trickle ICE**：候选收集过程中即时发送（批量限制 8 个/批次）
- ✅ **三状态 ACK**：区分在线转发、离线缓存、缓存满
- ⚠️ **连接管理**：需要心跳检测死连接，需要维护连接状态

### 与 COMPACT 模式对比

| 特性 | COMPACT (UDP) | RELAY (TCP) |
|------|--------------|-------------|
| **连接状态** | 无状态 | 长连接 |
| **重传** | 应用层 PEER_INFO_ACK | TCP 自动 |
| **负载大小** | < 1400 bytes | 无限制（建议<64KB） |
| **离线缓存** | 整包缓存 | 单候选缓存（最大255个） |
| **服务器清理** | 30秒无REGISTER | 60秒无消息 |
| **地址变化** | 主动推送 | N/A（TCP地址不变） |
| **寻址方式** | local_peer_id (32B) | name (32B) |

---

## 协议格式

所有消息共享 **9 字节包头**（网络字节序）：

```c
typedef struct {
    uint32_t magic;    // 0x50325030 ("P2P0")
    uint8_t  type;     // 消息类型（见下表）
    uint32_t length;   // 后续 Payload 长度（字节）
} p2p_relay_hdr_t;
```

### 消息类型

| 值 | 名称 | 方向 | Payload | 说明 |
|----|------|------|---------|------|
| 1 | P2P_RLY_LOGIN | C→S | `name[32]` | 客户端登录/注册 |
| 2 | P2P_RLY_LOGIN_ACK | S→C | 无 | 登录确认 |
| 3 | P2P_RLY_LIST | C→S | 无 | 请求在线用户列表 |
| 4 | P2P_RLY_LIST_RES | S→C | `"alice,bob,..."` | 返回用户列表（逗号分隔） |
| 5 | P2P_RLY_CONNECT | C→S | `target[32] + payload` | 发送候选给目标（主动方） |
| 6 | P2P_RLY_OFFER | S→C | `from[32] + payload` | 转发连接请求（推送给被动方） |
| 7 | P2P_RLY_ANSWER | C→S | `target[32] + payload` | 返回候选给发起方（被动方） |
| 8 | P2P_RLY_FORWARD | S→C | `from[32] + payload` | 转发应答给主动方 |
| 9 | P2P_RLY_HEARTBEAT | C→S | 无 | 心跳（保持连接） |
| 10 | P2P_RLY_CONNECT_ACK | S→C | `ack_t[4]` | 连接确认（三状态） |

> **注**：C = Client, S = Server

---

## 核心消息详解

### 1. LOGIN - 客户端登录

**结构**：
```c
typedef struct {
    char name[32];  // 客户端唯一标识符（用于寻址）
} p2p_relay_login_t;
```

**流程**：
```
Client                    Server
  |                          |
  |--- P2P_RLY_LOGIN ------->|
  |    name="alice"          | [记录映射: fd → name]
  |<-- P2P_RLY_LOGIN_ACK ----|
  |                          |
```

**服务器行为**：
- 检查名称是否已被占用（同名踢掉旧连接）
- 记录 `(fd → name)` 映射
- 响应 `P2P_RLY_LOGIN_ACK`
- 失败则关闭连接

---

### 2. CONNECT / CONNECT_ACK - 候选发送与确认 ⭐

这是 RELAY 模式的**核心机制**，支持 Trickle ICE 增量候选交换。

#### CONNECT 消息结构

```c
// Payload = 目标名称 + 信令负载头部 + N 个候选
[target_name: 32B]
[p2p_signaling_payload_hdr_t: 76B]  // sender, target, timestamp, count, ...
[candidates: N * 32B]               // ICE 候选列表
```

**信令负载头部**：
```c
typedef struct {
    char     sender[32];         // 发送方 peer_id
    char     target[32];         // 目标方 peer_id
    uint32_t timestamp;          // 时间戳（用于排序和去重）
    uint32_t delay_trigger;      // 延迟触发打洞（毫秒）
    int      candidate_count;    // 本次发送的候选数量
} p2p_signaling_payload_hdr_t;
```

**ICE 候选结构**：
```c
typedef struct {
    int                type;        // 候选类型（0=Host, 1=Srflx, 2=Relay, 3=Prflx）
    struct sockaddr_in addr;        // 传输地址（16B）
    struct sockaddr_in base_addr;   // 基础地址（16B）
    uint32_t           priority;    // 候选优先级
} p2p_candidate_t;  // 总大小 36 字节
```

#### CONNECT_ACK 响应（三状态设计）⭐

```c
typedef struct {
    uint8_t status;              // 0=在线, 1=离线有空间, 2=缓存已满
    uint8_t candidates_acked;    // 服务器确认的候选数量（0-255）
    uint8_t reserved[2];         // 保留字段
} p2p_relay_connect_ack_t;
```

**status 字段语义**：

| status | 含义 | candidates_acked | 客户端行为 |
|--------|------|------------------|-----------|
| **0** | 对端在线，已转发 | N（全部） | 继续 Trickle ICE |
| **1** | 对端离线，已缓存且有剩余空间 | N（全部） | 继续 Trickle ICE |
| **2** | 缓存已满 | 0 或 M（<N） | 停止发送，等待 FORWARD |

**关键设计**：
- `status=1` vs `status=2` 的区分：服务器在缓存候选后检查 `pending_count >= MAX_CANDIDATES`
- `candidates_acked` 支持**部分缓存**：发送 8 个，缓存 5 个（剩余 3 个等对端上线）
- 客户端根据 `candidates_acked` 更新 `next_candidate_index`，确保未确认的候选会重传

#### 边界条件验证

| 场景 | 服务器判断 | ACK 返回 | 客户端状态更新 |
|------|----------|---------|--------------|
| **对端在线** | `target_online=true` | `status=0, acked=N` | `next_index+=N, waiting=false` |
| **离线全缓存** | `pending<MAX, acked=N` | `status=1, acked=N` | `next_index+=N, waiting=false` |
| **部分缓存后满** | `pending=MAX-3, acked=3` | `status=2, acked=3` | `next_index+=3, waiting=true` |
| **之前就满** | `pending=MAX, acked=0` | `status=2, acked=0` | `next_index+=0, waiting=true` |

**服务器实现伪代码**：
```c
ack_status = 0;
candidates_acked = 0;

if (target_online) {
    // 在线：全部转发
    forward_all_to_target();
    ack_status = 0;
    candidates_acked = candidate_count;
} else {
    // 离线：尝试缓存
    for (i = 0; i < candidate_count; i++) {
        if (pending_count >= MAX_CANDIDATES) {
            ack_status = 2;  // 缓存满
            break;
        }
        cache_candidate(candidates[i]);
        pending_count++;
        candidates_acked++;
    }
    
    // 检查缓存后状态
    if (candidates_acked > 0) {
        if (pending_count >= MAX_CANDIDATES) {
            ack_status = 2;  // 本次缓存后满
        } else {
            ack_status = 1;  // 还有剩余空间
        }
    }
}

send_connect_ack(ack_status, candidates_acked);
```

**客户端处理逻辑**：
```c
switch (ack.status) {
    case 0:  // 在线转发
        waiting_for_peer = false;
        next_candidate_index += ack.candidates_acked;
        return ack.candidates_acked;  // 继续 Trickle ICE
        
    case 1:  // 离线有空间
        waiting_for_peer = false;
        next_candidate_index += ack.candidates_acked;
        return 0;  // 继续发送剩余候选
        
    case 2:  // 缓存已满
        waiting_for_peer = true;  // 停止发送
        next_candidate_index += ack.candidates_acked;  // 可能为 0
        return -2;  // 等待对端上线推送 FORWARD
}
```

---

### 3. OFFER / ANSWER / FORWARD - 消息转发

**消息转换规则**：

| 客户端发送 | 服务器转发为 | 原因 |
|-----------|-------------|------|
| P2P_RLY_CONNECT | P2P_RLY_OFFER | 通知被动方"有人要连接你" |
| P2P_RLY_ANSWER | P2P_RLY_FORWARD | 转发应答给主动方 |

**完整流程**：
```
Alice (主动方)         Server                Bob (被动方)
      |                   |                      |
      |--- CONNECT ------>|                      |
      | target="bob"      | [Bob 在线？]         |
      | candidates[0-7]   |                      |
      |<-- CONNECT_ACK ---|                      |
      | status=0, acked=8 |                      |
      |                   |--- OFFER ----------->|
      |                   | from="alice"         |
      |                   | candidates[0-7]      |
      |                   |                      |
      |                   |<-- ANSWER -----------|
      |                   | target="alice"       |
      |                   | candidates[0-5]      |
      |<-- FORWARD -------|                      |
      | from="bob"        |                      |
      | candidates[0-5]   |                      |
      |                   |                      |
      |<======== ICE 协商，建立 P2P 直连 ========>|
```

**Trickle ICE 增量发送**：
```
Alice                  Server                Bob
  |                      |                      |
  |--- CONNECT(cand 0-7)-->|--- OFFER(cand 0-7)-->|
  |<-- ACK(acked=8) ------|                      |
  |                      |                      |
  | [收集到新候选 8-9]    |                      |
  |--- CONNECT(cand 8-9)-->|--- OFFER(cand 8-9)-->|
  |<-- ACK(acked=2) ------|                      |
  |                      |                      |
  |                      |<-- ANSWER(cand 0-5)--|
  |<-- FORWARD(cand 0-5)--|                      |
  |                      |                      |
  |                      |<-- ANSWER(cand 6-7)--|
  |<-- FORWARD(cand 6-7)--|                      |
```

**批量发送限制**：
- 客户端每次最多发送 **8 个候选**（`MAX_CANDIDATES_PER_BATCH`）
- 目的：避免单个 TCP 包过大，提高重传效率
- Trickle ICE 本质：边收集边发送（每收集 1 个就发送 1 个）
- 批量限制仅用于**失败重传**场景（如超时后重发未确认的候选）

---

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
  |--- P2P_RLY_HEARTBEAT ---->|
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
    int    fd;
    char   name[32];
    time_t last_active;  // 最后活跃时间
    bool   valid;
} ice_client_t;

// 收到任何消息时更新
on_recv_message(client_idx) {
    g_ice_clients[client_idx].last_active = time(NULL);
}

// 定期清理（每 10 秒）
cleanup_ice_clients() {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (valid && (now - last_active) > ICE_CLIENT_TIMEOUT) {
            close(fd);
            valid = false;
        }
    }
}
```

**客户端建议**：
- 每 **30 秒**发送一次心跳
- 服务器 **60 秒**超时（留 2 倍余量）
- 也可通过其他消息（LIST、CONNECT）刷新活跃时间

---

## 离线候选缓存机制

### 缓存策略

**服务器端数据结构**：
```c
typedef struct {
    int                  fd;
    char                 name[32];
    time_t               last_active;
    bool                 valid;
    
    // 离线缓存（单候选粒度）
    p2p_candidate_t      pending_candidates[MAX_CANDIDATES];  // 队列
    int                  pending_count;                       // 当前数量
    char                 pending_from[32];                    // 来源 peer_id
} ice_client_t;
```

**缓存触发条件**：
1. 收到 `P2P_RLY_CONNECT` 且目标离线（`target->valid=false`）
2. 缓存未满（`pending_count < MAX_CANDIDATES`）

**缓存推送时机**：
- 对端上线（收到 `P2P_RLY_LOGIN`）时
- 服务器检查该用户是否有待推送的候选
- 如果有，立即发送 `P2P_RLY_OFFER` 推送缓存队列

**缓存限制**：
- 每用户最大缓存 **255 个候选**（`uint8_t candidates_acked`）
- FIFO 队列（先进先出）
- 超过限制时，新候选被拒绝（`status=2, candidates_acked=0`）

### 离线场景完整流程

```
Alice                  Server                Bob (离线)
  |                      |                      |
  |--- LOGIN ----------->|                      |
  |<-- LOGIN_ACK --------|                      |
  |                      |                      |
  |--- CONNECT(bob) ---->|                      |
  | candidates[0-7]      | [Bob 离线，缓存]     |
  |<-- CONNECT_ACK ------|  pending_count=8     |
  | status=1, acked=8    |                      |
  |                      |                      |
  |--- CONNECT(bob) ---->|                      |
  | candidates[8-15]     | [继续缓存]           |
  |<-- CONNECT_ACK ------|  pending_count=16    |
  | status=1, acked=8    |                      |
  |                      |                      |
  | ... (Alice 继续发送) |                      |
  |                      |                      |
  |                      |<-- LOGIN ------------|
  |                      |--- LOGIN_ACK -------->|
  |                      | [检测到缓存]         |
  |                      |--- OFFER ----------->|
  |                      | from="alice"         |
  |                      | candidates[0-15]     |
  |                      | [清空缓存]           |
  |                      |                      |
  |                      |<-- ANSWER -----------|
  |<-- FORWARD ----------|                      |
  |                      |                      |
  |<======== ICE 协商，建立 P2P 直连 ==========>|
```

---

## 错误处理

| 错误场景 | 服务器行为 | 客户端建议 |
|---------|----------|-----------|
| 收到无效 magic | 关闭连接 | 检查协议版本 |
| 目标用户不在线且缓存满 | 返回 `status=2, acked=0` | 停止发送，等待 FORWARD |
| Payload 过大 (>65536) | 关闭连接 | 限制候选批量大小 |
| recv 失败 | 关闭连接，标记 `valid=false` | 重连并重新 LOGIN |
| 客户端超时（60秒无消息） | 主动关闭连接 | 发送心跳 |
| 同名登录 | 踢掉旧连接，接受新连接 | 检测到断开后重连 |

---

## 设计注意事项

### 1. Trickle ICE 批量大小的权衡

**问题**：每次应该发送多少个候选？

**设计选择**：
- **1 个/批次**：真正的 Trickle ICE（边收集边发送）
  - ✅ 最低延迟（收集到立即发送）
  - ⚠️ TCP 包数量多，开销大
  
- **8 个/批次**（当前实现）：批量发送
  - ✅ 减少 TCP 包数量
  - ✅ 适合重传场景（失败后批量重发）
  - ⚠️ 增加首次候选发送延迟

**实际策略**：
- 正常 Trickle ICE：收集 1-2 个就发送（实时性优先）
- 失败重传：批量发送最多 8 个（效率优先）
- 通过 `MAX_CANDIDATES_PER_BATCH` 限制批量大小

### 2. 候选状态追踪的隐式假设

**当前实现假设**：
- 服务器**按顺序缓存前 N 个候选**
- 客户端通过 `next_candidate_index += candidates_acked` 更新索引

**隐患**：
```
发送候选 [0-7] (8个)
服务器只缓存 [0-4] (5个)，返回 acked=5

客户端：next_candidate_index = 0 + 5 = 5
下次发送：候选 [5-12]

问题：候选 5, 6, 7 在第一批中发送了但未被确认
     下次从索引 5 开始，候选 5, 6, 7 会被重发 ✅ 正确！
```

**验证**：当前实现**正确**，因为：
1. 服务器确实按顺序缓存（循环从 `i=0` 开始）
2. `candidates_acked` 表示"前 N 个候选成功"
3. 未确认的候选（索引 ≥ next_candidate_index）会在下次重发

**脆弱性**：
- 依赖服务器"按顺序缓存"的隐式假设
- 如果服务器改为随机缓存，客户端逻辑会失效

**改进方向**（未实现）：
- 每个候选独立状态追踪（PENDING/SENDING/ACKED/FAILED）
- 协议返回位图或索引范围（明确告知哪些候选成功）
- 见架构文档中的"候选状态机设计"章节

### 3. `status=1` vs `status=2` 的必要性

**问题**：为什么需要区分"有空间"和"已满"？

**原因**：
- `status=1`：客户端可以**继续 Trickle ICE**（服务器还能缓存）
- `status=2`：客户端应该**停止发送**（避免浪费带宽）

**边界条件**：
- 发送 8 个，缓存 5 个，剩余 10 个空间 → `status=1`（继续发送）
- 发送 8 个，缓存 5 个，剩余 0 个空间 → `status=2`（停止发送）

**实现**：
```c
if (candidates_acked > 0 && pending_count >= MAX_CANDIDATES) {
    ack_status = 2;  // 本次缓存后满
} else if (candidates_acked > 0) {
    ack_status = 1;  // 还有剩余空间
}
```

---

## 完整示例：WebRTC ICE 协商

```
Alice (浏览器)          Server              Bob (浏览器)
     |                     |                      |
     |--- LOGIN ------------->|                      |
     |<-- LOGIN_ACK ----------|                      |
     |                     |<---- LOGIN -------------|
     |                     |--- LOGIN_ACK ---------->|
     |                     |                      |
     | [创建 RTCPeerConnection, 生成 offer]       |
     |                     |                      |
     |--- CONNECT ---------->|                      |
     | candidates[0-7]     |                      |
     |<-- CONNECT_ACK ------|                      |
     | status=0, acked=8   |                      |
     |                     |--- OFFER ------------->|
     |                     | candidates[0-7]      |
     |                     |                      |
     | [收集新候选 8-9]     |                      |
     |--- CONNECT ---------->|                      |
     | candidates[8-9]     |                      |
     |<-- CONNECT_ACK ------|                      |
     | status=0, acked=2   |                      |
     |                     |--- OFFER ------------->|
     |                     | candidates[8-9]      |
     |                     |                      |
     |                     | [Bob 生成 answer]    |
     |                     |                      |
     |                     |<-- ANSWER ------------|
     |                     | candidates[0-5]      |
     |<-- FORWARD ----------|                      |
     | candidates[0-5]     |                      |
     |                     |                      |
     | [设置 remote description, ICE 协商开始]    |
     |<===== STUN/TURN 打洞，建立 P2P 连接 =======>|
     |                     |                      |
     |--- HEARTBEAT -------->|<---- HEARTBEAT ------|
     | (每 30 秒保持连接)   |                      |
```

---

---

# 第三部分：PUBSUB 模式 (HTTPS)

## 概述

PUBSUB 模式使用 **GitHub Gist** 作为信令通道，通过 HTTPS Polling 进行候选交换。
双方扮演不同角色：**PUB（发起端）** 写入 offer，**SUB（订阅端）** 轮询读取 offer 后写入 answer。

### 核心特性

- ✅ **零部署**：无需自建服务器（利用 GitHub 基础设施）
- ✅ **快速原型**：适合演示和测试
- ✅ **内置加密**：DES + Base64 保护候选信息隐私
- ✅ **天然持久化**：数据存于 Gist，不受网络抖动影响，天然支持异步场景
- ⚠️ **轮询模式**：建连延迟受轮询间隔影响（PUB 默认 1s，SUB 默认 5s，均可通过宏调节）
- ⚠️ **API 限制**：依赖 GitHub API 速率限制

## 工作原理

双端通过同一个 Gist 文件的两个字段交换信息：

```json
{
  "offer":  "<PUB 的 ICE 候选，DES 加密后 Base64 编码>",
  "answer": "<SUB 的 ICE 候选，DES 加密后 Base64 编码>"
}
```

**完整信令流程**：

```
PUB                    GitHub Gist                    SUB
 |                          |                           |
 |--- PATCH offer --------->|                           |  [1]
 |    (PUB 的 ICE 候选)     |                           |
 |                          |   .--- tick() 每 5s ----> |
 |                          |<------- GET (轮询) -------|
 |                          |   If-None-Match: ETag     |
 |                          |--- 304 Not Modified ----->|  (offer 未更新)
 |                          |   .--- tick() 每 5s ----> |
 |                          |<------- GET (轮询) -------|
 |                          |-------- 200 OK ---------->|  (offer 有新内容)
 |                          |                           |
 |                          |   [2] SUB 解密 offer     |
 |                          |       添加远端候选        |
 |                          |                           |
 |                          |<------- PATCH answer -----|
 |                          |       (SUB 的 ICE 候选)   |
 |                          |                           |
 |   .--- tick() 每 1s ---> |                           |
 |<------- GET (轮询) ------|                           |
 |--- 304 Not Modified ---->|                           |  (answer 未更新)
 |   .--- tick() 每 1s ---> |                           |
 |<------- GET (轮询) ------|                           |
 |<-------- 200 OK ---------|                           |  (answer 有新内容)
 |                          |                           |
 |  [3] PUB 解密 answer     |                           |
 |      添加远端候选         |                           |
 |                          |                           |
 |<======= ICE 连通性检查（直连 / STUN 打洞）==========>|
```

**步骤说明**：
- **[1]** PUB 调用 `p2p_signal_pubsub_send()` 将加密候选写入 `offer` 字段
- **[2]** SUB 通过 `p2p_signal_pubsub_tick()` 每 5s（`P2P_PUBSUB_SUB_POLL_MS`）轮询，自动写入 `answer`（仅一次）
- **[3]** PUB 通过 `p2p_signal_pubsub_tick()` 每 1s（`P2P_PUBSUB_PUB_POLL_MS`）轮询，尽快检测 answer

**ETag 优化**：每次 GET 携带 `If-None-Match: <etag>`，Gist 未变化时返回 304 Not Modified，节省流量。

## 数据格式

**加密编码流程**：

```
p2p_signaling_payload_t (76B header + N×32B candidates)
        ↓  p2p_des_encrypt(ctx.auth_key)
  DES 加密密文（ECB 模式，8 字节块对齐）
        ↓  p2p_base64_encode()
  Base64 字符串
        ↓  JSON 转义
  "offer" / "answer" 字段值
```

**轮询间隔宏**（可编译期覆盖）：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `P2P_PUBSUB_PUB_POLL_MS` | 1000 ms | PUB 轮询 answer 间隔，宜短以降低建连延迟 |
| `P2P_PUBSUB_SUB_POLL_MS` | 5000 ms | SUB 轮询 offer 间隔，offer 写入后等待时间较长 |

**模块**：`p2p_signal_pubsub.c/h`

---

## 附录：协议包类型完整列表

### P2P 直连协议 (0x01-0x7F)

| 范围 | 分类 | 包类型 | 说明 |
|------|------|--------|------|
| **0x01-0x0F** | **打洞与安全** | | |
| 0x01 | P2P_PKT_PUNCH | NAT 打洞包 |
| 0x02 | P2P_PKT_PUNCH_ACK | NAT 打洞确认 |
| 0x03 | P2P_PKT_AUTH | 安全握手包 |
| **0x10-0x1F** | **保活协议** | | |
| 0x10 | P2P_PKT_PING | 心跳请求 |
| 0x11 | P2P_PKT_PONG | 心跳响应 |
| **0x20-0x2F** | **数据传输** | | |
| 0x20 | P2P_PKT_DATA | 应用数据 |
| 0x21 | P2P_PKT_ACK | 数据确认 |
| 0x22 | P2P_PKT_FIN | 连接关闭 |
| **0x30-0x3F** | **路由探测** | | |
| 0x30 | P2P_PKT_ROUTE_PROBE | 路由探测包 |
| 0x31 | P2P_PKT_ROUTE_PROBE_ACK | 路由探测确认 |

### COMPACT 信令协议 (0x80-0xBF)

| 范围 | 包类型 | 说明 |
|------|--------|------|
| **0x80-0x8F** | **核心信令** | |
| 0x80 | SIG_PKT_REGISTER | 注册到信令服务器 |
| 0x81 | SIG_PKT_REGISTER_ACK | 注册确认 |
| 0x82 | SIG_PKT_PEER_INFO | 候选列表同步包 |
| 0x83 | SIG_PKT_PEER_INFO_ACK | 候选列表确认 |
| 0x84 | SIG_PKT_NAT_PROBE | NAT 类型探测请求 |
| 0x85 | SIG_PKT_NAT_PROBE_ACK | NAT 类型探测响应 |
| **0xA0-0xAF** | **中继扩展** | |
| 0xA0 | P2P_PKT_RELAY_DATA | 中继服务器转发的数据 |
| 0xA1 | P2P_PKT_RELAY_ACK | 中继服务器转发的 ACK |

### RELAY 信令协议 (TCP)

| 值 | 名称 | 说明 |
|----|------|------|
| 1 | P2P_RLY_LOGIN | 登录/注册 |
| 2 | P2P_RLY_LOGIN_ACK | 登录确认 |
| 3 | P2P_RLY_LIST | 查询在线用户 |
| 4 | P2P_RLY_LIST_RES | 用户列表响应 |
| 5 | P2P_RLY_CONNECT | 发送候选给目标 |
| 6 | P2P_RLY_OFFER | 转发连接请求 |
| 7 | P2P_RLY_ANSWER | 返回候选给发起方 |
| 8 | P2P_RLY_FORWARD | 转发应答 |
| 9 | P2P_RLY_HEARTBEAT | 心跳 |
| 10 | P2P_RLY_CONNECT_ACK | 连接确认（三状态） |

---

## 安全注意事项

> [!CAUTION]
> 当前协议设计侧重功能完整性，**不含强加密或身份认证机制**。

### 通用安全建议

| 威胁 | 解决方案 |
|------|---------|
| **窃听** | 启用 DTLS/TLS 加密（库已支持，见 `use_dtls` 配置） |
| **身份冒用** | 添加认证 token、签名机制 |
| **重放攻击** | 添加 nonce/timestamp 验证 |
| **DDoS** | Rate limiting、IP 白名单 |
| **中间人攻击** | 端到端加密、证书验证 |

### 模式特定风险

#### COMPACT 模式
- ❌ **无 peer_id 验证**：任何人可冒充他人注册
- ❌ **明文传输**：地址信息未加密（建议应用层加密）
- ⚠️ **UDP 泛洪**：需要服务器端 rate limiting

#### RELAY 模式
- ❌ **无登录验证**：任何人可连接服务器
- ❌ **候选劫持**：中间人可修改 ICE candidates
- ✅ **死连接检测**：已通过心跳机制解决

#### PUBSUB 模式
- ✅ **DES 加密**：候选信息已加密（基础保护）
- ⚠️ **Gist 可见性**：Public Gist 可被他人访问
- ⚠️ **依赖 GitHub**：服务可用性依赖第三方

### 生产部署 Checklist

- [ ] 启用 DTLS/TLS 加密（端到端）
- [ ] 实现 token 认证机制（登录验证）
- [ ] 配置防火墙规则（限制访问）
- [ ] 部署 rate limiting（建议：100 req/min/IP）
- [ ] 监控异常连接模式（DDoS 检测）
- [ ] 定期更新依赖库（mbedtls/openssl/usrsctp）
- [ ] 记录审计日志（安全事件追踪）

---

## 参考资料

- **协议定义**：`include/p2pp.h`
- **服务器实现**：`p2p_server/server.c`
- **客户端实现**：
  - COMPACT: `src/p2p_signal_compact.c/.h`
  - RELAY: `src/p2p_signal_relay.c/.h`
  - PUBSUB: `src/p2p_signal_pubsub.c/.h`
- **架构文档**：`doc/ARCHITECTURE.md`
- **测试用例**：`test/` 目录

**最后更新**：2026-02-18  
**协议版本**：1.0
