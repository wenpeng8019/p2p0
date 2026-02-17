# P2P 连接 API 使用指南

## 概述

P2P 库提供三种信令模式，通过统一的 `p2p_connect()` 接口连接：

| 模式 | 枚举值 | 信令方式 | 适用场景 |
|------|--------|---------|---------|
| **COMPACT** | `P2P_SIGNALING_MODE_COMPACT` | UDP 无状态 | 轻量级应用、简单 NAT |
| **RELAY** | `P2P_SIGNALING_MODE_RELAY` | TCP 长连接 | 生产环境、复杂 NAT |
| **PUBSUB** | `P2P_SIGNALING_MODE_PUBSUB` | GitHub Gist | 无服务器、演示测试 |

## API 接口

### 核心接口

```c
/* 创建会话 */
p2p_session_t *p2p_create(const p2p_config_t *cfg);

/* 发起连接 */
int p2p_connect(p2p_session_t *s, const char *remote_peer_id);

/* 更新状态机 (单线程模式需周期调用) */
int p2p_update(p2p_session_t *s);

/* 发送/接收数据 */
int p2p_send(p2p_session_t *s, const void *buf, int len);
int p2p_recv(p2p_session_t *s, void *buf, int len);

/* 查询状态 */
int p2p_state(const p2p_session_t *s);  // P2P_STATE_*
int p2p_path(const p2p_session_t *s);   // P2P_PATH_*

/* 关闭/销毁 */
void p2p_close(p2p_session_t *s);
void p2p_destroy(p2p_session_t *s);
```

### 配置结构 (p2p_config_t)

```c
typedef struct {
    /* 基础配置 */
    uint16_t    bind_port;                  // 本地 UDP 端口 (0 = 自动)
    char        local_peer_id[P2P_PEER_ID_MAX];   // 本端身份标识
    
    /* 信令配置 */
    int         signaling_mode;             // P2P_CONNECT_MODE_*
    const char* server_host;                // 信令服务器 (SIMPLE/ICE)
    uint16_t    server_port;                // 信令服务器端口
    const char* gh_token;                   // GitHub Token (PUBSUB)
    const char* gist_id;                    // Gist ID (PUBSUB)
    
    /* ICE/STUN/TURN 配置 */
    bool        use_ice;                    // 1 = 启用 ICE 协议栈
    const char* stun_server;                // STUN 服务器
    uint16_t    stun_port;                  // STUN 端口 (默认 3478)
    const char* turn_server;                // TURN 服务器 (可选)
    uint16_t    turn_port;                  // TURN 端口
    const char* turn_user;                  // TURN 用户名
    const char* turn_pass;                  // TURN 密码
    
    /* 传输选项 */
    bool        use_pseudotcp;              // 1 = 启用拥塞控制
    bool        use_dtls;                   // 1 = 启用 DTLS 加密
    bool        enable_tcp;                 // 1 = 尝试 TCP 打洞
    
    /* 事件回调 */
    p2p_on_connected_fn    on_connected;    // 连接建立回调
    p2p_on_disconnected_fn on_disconnected; // 连接断开回调
    p2p_on_data_fn         on_data;         // 数据到达回调
    void*                  userdata;        // 用户数据
    
    /* 调试选项 */
    bool        disable_lan_shortcut;       // 禁止同子网直连
    bool        verbose_nat_punch;          // 输出详细 NAT 打洞日志
} p2p_config_t;
```

---

## 信令模式详解

### 模式 1: SIMPLE (简单信令)

**原理**: 双方各自向 UDP 信令服务器注册 `<local_id, remote_id>` 映射，服务器匹配成功后返回对方地址，开始 NAT 打洞。

**特点**:
- ✅ 服务器无状态，易于扩展
- ✅ 协议简单（64 字节注册包）
- ⚠️ 打洞成功率低于 ICE
- ⚠️ 失败时自动降级为中继

**配置要求**: `server_host`, `server_port`

**连接规则**: `remote_peer_id` 必须非 NULL，指定对方的明确 local_peer_id

```c
p2p_config_t cfg = {0};
cfg.signaling_mode = P2P_SIGNALING_MODE_COMPACT;
cfg.server_host = "signal.example.com";
cfg.server_port = 8888;
strncpy(cfg.local_peer_id, "alice", P2P_PEER_ID_MAX);

p2p_session_t *s = p2p_create(&cfg);
p2p_connect(s, "bob");  // 连接到 bob

while (p2p_state(s) != P2P_STATE_CONNECTED) {
    p2p_update(s);
    usleep(10000);
}
p2p_send(s, "Hello", 5);
```

---

### 模式 2: ICE (有状态信令)

**原理**: 通过 TCP 长连接登录信令服务器，服务器转发 ICE 候选者（offer/answer），支持一对多场景。

**特点**:
- ✅ 标准 ICE 协议 (RFC 5245)
- ✅ 高打洞成功率（通过 STUN 获取公网地址）
- ✅ 支持 TURN 中继作为 fallback
- ⚠️ 需要 STUN/TURN 服务器
- ⚠️ 信令服务器需维护 TCP 长连接

**配置要求**: `server_host`, `server_port`, 可选 `stun_server`, `turn_server`

**连接规则**:
- `remote_peer_id` 非 NULL → 主动发起，发送 offer 到指定目标
- `remote_peer_id` = NULL → 被动等待，接受来自任意对等方的 offer

```c
// 主动方 (Alice)
p2p_config_t cfg = {0};
cfg.signaling_mode = P2P_SIGNALING_MODE_RELAY;
cfg.server_host = "relay.example.com";
cfg.server_port = 8888;
cfg.stun_server = "stun.l.google.com";
cfg.stun_port = 3478;
strncpy(cfg.local_peer_id, "alice", P2P_PEER_ID_MAX);

p2p_session_t *s = p2p_create(&cfg);
p2p_connect(s, "bob");  // 主动连接 bob
```

```c
// 被动方 (Bob)
p2p_config_t cfg = {0};
cfg.signaling_mode = P2P_SIGNALING_MODE_RELAY;
cfg.server_host = "relay.example.com";
cfg.server_port = 8888;
strncpy(cfg.local_peer_id, "bob", P2P_PEER_ID_MAX);

p2p_session_t *s = p2p_create(&cfg);
p2p_connect(s, NULL);  // 被动等待任意连接
```

---

### 模式 3: PUBSUB (公共信令)

**原理**: 使用 GitHub Gist 作为信令中介。发布者 (PUB) 写入 offer，订阅者 (SUB) 读取并回复 answer。

**特点**:
- ✅ 无需自建信令服务器
- ✅ 适合临时测试、演示
- ✅ 支持防火墙后的设备
- ⚠️ 依赖第三方服务（GitHub API 限流）
- ⚠️ 轮询延迟高（约 10 秒）
- ⚠️ 不适合生产环境

**配置要求**: `gh_token`, `gist_id`

**连接规则**:
- `remote_peer_id` 非 NULL → PUB 角色，发布 offer 等待对方 answer
- `remote_peer_id` = NULL → SUB 角色，监听 offer 并自动回复 answer

```c
// PUB 角色 (Bob - 主动发起)
p2p_config_t cfg = {0};
cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;
cfg.gh_token = "ghp_xxxxxxxxxxxx";
cfg.gist_id = "abc123def456";
cfg.stun_server = "stun.l.google.com";
cfg.stun_port = 3478;
strncpy(cfg.local_peer_id, "bob", P2P_PEER_ID_MAX);

p2p_session_t *s = p2p_create(&cfg);
p2p_connect(s, "alice");  // PUB: 发布 offer 给 alice
```

```c
// SUB 角色 (Alice - 被动等待)
p2p_config_t cfg = {0};
cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;
cfg.gh_token = "ghp_xxxxxxxxxxxx";
cfg.gist_id = "abc123def456";
cfg.stun_server = "stun.l.google.com";
cfg.stun_port = 3478;
strncpy(cfg.local_peer_id, "alice", P2P_PEER_ID_MAX);

p2p_session_t *s = p2p_create(&cfg);
p2p_connect(s, NULL);  // SUB: 监听任意 offer
```

**PUBSUB 架构图**:
```
┌─────────┐         ┌──────────────┐         ┌─────────┐
│   Bob   │ ──PUB─→ │  GitHub Gist │ ←─SUB── │  Alice  │
│  (PUB)  │         │   (offer)    │         │  (SUB)  │
└────┬────┘         └──────────────┘         └────┬────┘
     │                                             │
     │              ┌──────────────┐               │
     └──────────←── │  GitHub Gist │ ──────────→───┘
                    │   (answer)   │
                    └──────────────┘
```

---

## 连接状态

```c
enum {
    P2P_STATE_IDLE = 0,         // 初始状态
    P2P_STATE_REGISTERING,      // 注册到信令服务器
    P2P_STATE_PUNCHING,         // NAT 打洞中
    P2P_STATE_CONNECTED,        // 已连接
    P2P_STATE_RELAY,            // 中继模式
    P2P_STATE_CLOSING,          // 关闭中
    P2P_STATE_CLOSED,           // 已关闭
    P2P_STATE_ERROR             // 错误状态
};
```

## 连接路径

```c
enum {
    P2P_PATH_NONE = 0,          // 未连接
    P2P_PATH_LAN,               // 同一子网，直连
    P2P_PATH_PUNCH,             // NAT 打洞成功
    P2P_PATH_RELAY              // 服务器中继 (fallback)
};
```

---

## 事件回调

```c
// 连接建立回调
void on_connected(p2p_session_t *s, void *userdata) {
    printf("Connected! Path: %d\n", p2p_path(s));
}

// 连接断开回调
void on_disconnected(p2p_session_t *s, void *userdata) {
    printf("Disconnected!\n");
}

// 数据到达回调 (可选，替代 p2p_recv)
void on_data(p2p_session_t *s, const void *data, int len, void *userdata) {
    printf("Received %d bytes\n", len);
}

// 使用回调
p2p_config_t cfg = {0};
cfg.on_connected = on_connected;
cfg.on_disconnected = on_disconnected;
cfg.on_data = on_data;
cfg.userdata = my_context;
```

---

## 完整示例

### COMPACT 模式 (双向)

```c
// === alice.c ===
p2p_config_t cfg = {0};
cfg.signaling_mode = P2P_SIGNALING_MODE_COMPACT;
cfg.server_host = "127.0.0.1";
cfg.server_port = 8888;
strncpy(cfg.local_peer_id, "alice", P2P_PEER_ID_MAX);

p2p_session_t *s = p2p_create(&cfg);
p2p_connect(s, "bob");

while (p2p_state(s) != P2P_STATE_CONNECTED) {
    p2p_update(s);
    usleep(10000);
}

p2p_send(s, "Hello Bob!", 10);
p2p_destroy(s);
```

```c
// === bob.c ===
p2p_config_t cfg = {0};
cfg.signaling_mode = P2P_SIGNALING_MODE_COMPACT;
cfg.server_host = "127.0.0.1";
cfg.server_port = 8888;
strncpy(cfg.local_peer_id, "bob", P2P_PEER_ID_MAX);

p2p_session_t *s = p2p_create(&cfg);
p2p_connect(s, "alice");

while (p2p_state(s) != P2P_STATE_CONNECTED) {
    p2p_update(s);
    usleep(10000);
}

char buf[256];
int n = p2p_recv(s, buf, sizeof(buf));
printf("Received: %.*s\n", n, buf);
p2p_destroy(s);
```

---

## 模式对比

| 特性 | SIMPLE | ICE | PUBSUB |
|------|--------|-----|--------|
| 信令协议 | UDP 无状态 | TCP 长连接 | HTTP + Gist API |
| 服务器要求 | p2p_server | p2p_server | GitHub Token |
| 打洞成功率 | 中等 | 高 | 高 |
| 延迟 | 低 | 低 | 高 (轮询) |
| NAT 兼容性 | 基础 | 优秀 | 优秀 |
| 适用场景 | 轻量级应用 | 生产环境 | 演示/测试 |

---

## 示例程序

- [p2p_ping/p2p_ping.c](p2p_ping/p2p_ping.c) - 支持所有三种模式的测试工具
- [p2p_server/server.c](p2p_server/server.c) - 信令服务器 (支持 SIMPLE + ICE)

## 测试指南

详细测试说明请参考 [test/README.md](test/README.md)

```bash
# 运行单元测试
cd test
make && ./test_transport && ./test_compact_server && ./test_relay_server

# 运行集成测试
./test_client_integration.sh
./test_nat_punch.sh
```
