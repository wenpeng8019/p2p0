# GitHub Gist 信令机制详解

**创新方案：** 将 GitHub Gist 作为无服务器 P2P 信令通道

---

## 1. 设计理念

### 1.1 传统信令服务器的痛点

传统 P2P 应用需要部署专用信令服务器来交换 SDP (Session Description Protocol) 信息：

```
┌─────────┐                     ┌─────────────┐                     ┌─────────┐
│ Peer A  │ ──── Offer ──────→ │   Signaling │ ──── Offer ──────→ │ Peer B  │
│         │                     │    Server   │                     │         │
│         │ ←─── Answer ─────  │   (自建)    │ ←─── Answer ─────  │         │
└─────────┘                     └─────────────┘                     └─────────┘
```

对于个人用户来说，不但要维护一个完整的信令服务器实现，而且还会带来持续的成本开销。


### 1.2 GitHub Gist 作为信令通道

**核心思想：** 利用 GitHub Gist 的 RESTful API 作为轻量级键值存储

```
┌─────────┐                     ┌─────────────┐                     ┌─────────┐
│ Peer A  │ ───── PATCH ─────→ │ GitHubGist  │ ←──── GET ──────── │ Peer B  │
│ 订阅者  │ ←──── GET ──────── │ (JSON文件)  │ ───── PATCH ─────→ │ 发布者  │
└─────────┘                     └─────────────┘                     └─────────┘
              轮询检测更新       免费、全球CDN        发布信令
```

**优势：**
- ✅ **零成本** - GitHub 提供免费 Gist 服务
- ✅ **零运维** - 完全托管，无需维护
- ✅ **全球可达** - GitHub CDN 覆盖全球（Cloudflare + Fastly）
- ✅ **高可用** - GitHub 99.9% SLA 保障
- ✅ **简单** - 只需 GitHub 账号和 Personal Access Token

---

## 2. 工作原理

### 2.1 信令交换流程（信箱 + 发布板机制）

每个对等方拥有独立的 Gist，在不同阶段扮演不同角色：
- **心跳**：作为 SUB 方时，Gist 初始写入时间戳标识自己在线，每 5 分钟刷新
- **发布板**：收到 offer 后，各方的 Gist 转为发布自己的候选列表

```
时间轴 (Timeline)
═══════════════════════════════════════════════════════════════════════════

t0: 初始化
┌─────────────────┐                                     ┌─────────────────┐
│  Alice (SUB)    │                                     │  Bob (PUB)      │
│  gist_a = 心跳  │                                     │  gist_b         │
└────────┬────────┘                                     └────────┬────────┘
         │                                                       │
         │ 1. connect(NULL)                                      │ connect(gist_a)
         │    写入心跳时间戳到 gist_a                            │    开始收集 ICE 候选
         │    "ONLINE:<timestamp>"                               │    （STUN 后台运行）
         │    每 5 分钟自动刷新                                  │
         │                                                       │
         │ 2. 每 5s 轮询自己的 gist_a                            │
         │    GET /gists/gist_a → 等待 offer                    │
         │                                                       │
         │                                          3. 读取 gist_a 时间戳
         │                                             检查 SUB 是否在线
         │                                             （>5min 未刷新则警告）
         │                                             投递 offer 到 gist_a
         │                                             PATCH /gists/gist_a
         │                                             content: "OFFER:gist_b"
         │                                                       │
t1: Offer 投递完成                                               │
         │                                          4. 每 1s 轮询 gist_a
         │                                             double check:
         │                                             若读到 "ONLINE:" 说明
         │                                             心跳覆盖了 offer → 重发
         │ 5. 轮询 gist_a 检测到 offer                           │    等待 Alice 响应
         │    GET /gists/gist_a                                  │
         │    content: "OFFER:gist_b"                            │
         │    → 提取 remote_gist_id = gist_b                    │
         │    → 启动候选同步                                    │
         │                                                       │
         │ 6. 发布自己的候选到 gist_a                            │
         │    PATCH /gists/gist_a                                │
         │    content: Base64(DES(SDP candidates))               │
         │    （覆盖 offer，gist_a 转为发布板）                  │
         │                                                       │
t2: SUB 响应完成                                                 │
         │                                          7. 检测到 gist_a 不再是 offer
         │                                             而是候选数据
         │ 8. 每 1s 轮询 gist_b                        → 解密 Alice 的候选
         │    等待 Bob 的候选                          → 发布自己的候选到 gist_b
         │                                             PATCH /gists/gist_b
         │                                             content: Base64(DES(SDP candidates))
         │                                                       │
t3: PUB 候选已发布
         │                                                       │
         │ 9. 轮询 gist_b 获取 Bob 的候选                        │
         │    GET /gists/gist_b                                  │
         │    → Base64 解码 → DES 解密 → SDP 解析             │
         │    → 注入 remote_cands                               │
         │                                                       │
         │ ◄══════════ 10. ICE 连通性检查 ═══════════════════► │
         │                UDP hole punching                      │
         │                STUN binding requests                  │
         │                                                       │
t4: P2P 连接建立
         │                                                       │
         │ ◄════════════ 11. 直连数据通道 ═════════════════►   │
         │                绕过 GitHub                            │
         │                应用层协议 (PING/PONG)                 │
         │                                                       │
```

### 2.2 Gist 数据格式

**Gist 文件名：** `p2p_signal.json`

每个 Gist 在不同阶段承载不同内容：

| 阶段 | content 内容 | 说明 |
|------|-------------|------|
| **心跳** | `"ONLINE:<timestamp>:<peer_id>"` | SUB 方上线标识，每 5 分钟刷新 |
| **Offer** | `"OFFER:<gist_id>:<peer_id>"` | PUB 投递到 SUB 的信箱 |
| **候选列表** | `Base64(DES(SDP candidates))` | 各方发布自己的候选 |

**心跳格式（明文）：**
```
ONLINE:1713100800:alice
```
- `ONLINE:` — 固定前缀，标识 SUB 在线状态
- `<timestamp>` — Unix 时间戳（秒），PUB 据此判断 SUB 是否仍在线
- `<peer_id>` — SUB 的 peer_id，PUB 据此知道对端身份（gist_id 的别名）
- PUB 检测时间戳超过 5 分钟未刷新则打印警告

**Offer 格式（明文）：**
```
OFFER:abc123def456789:bob
```
- `OFFER:` — 固定前缀，用于区分 offer 和候选数据
- `<gist_id>` — PUB 方的 Gist ID，SUB 据此知道去哪里轮询 PUB 的候选
- `<peer_id>` — PUB 方的 peer_id，SUB 据此知道对端身份

**候选列表格式（SDP 协议）：**
```
p2p_ice_export_sdp()  →  DES 加密  →  Base64 编码
```

加密前的 SDP 明文示例：
```
a=candidate:1 1 udp 2130706431 192.168.1.100 12345 typ host
a=candidate:2 1 udp 1694498815 203.0.113.5 54321 typ srflx raddr 192.168.1.100 rport 12345
a=candidate:3 1 udp 16777215 198.51.100.1 3478 typ relay raddr 203.0.113.5 rport 54321
```

采用标准 ICE SDP 格式（RFC 8839），与 p2p_ice 模块的应用层 API 共用同一套
序列化/反序列化逻辑，确保与外部 ICE 实现的互操作性。

### 2.3 序列化与加密

**候选数据处理流程：**

```
候选数组 (p2p_candidate_t[])
    ↓
┌───────────────────────────────────────┐
│ 1. SDP 序列化 (p2p_ice_export_sdp)    │
│    - 标准 ICE SDP 格式 (RFC 8839)     │
│    - a=candidate:... 行               │
│    输出: char sdp_text[...]           │
└───────────────────────────────────────┘
    ↓
┌───────────────────────────────────────┐
│ 2. 加密 (p2p_des_encrypt)             │
│    - 从 auth_key 派生 8 字节密钥      │
│    - 8 字节块对齐                     │
│    输出: uint8_t encrypted[padded]    │
└───────────────────────────────────────┘
    ↓
┌───────────────────────────────────────┐
│ 3. Base64 编码                        │
│    输出: char base64[...]             │
└───────────────────────────────────────┘
    ↓
┌───────────────────────────────────────┐
│ 4. PATCH 到 Gist (自己的发布板)       │
│    content: "<base64_string>"         │
└───────────────────────────────────────┘
```

**反向处理（接收方）：**
```
GET 对方 Gist → 提取 content → Base64 解码 → DES 解密 → p2p_ice_import_sdp() → remote_cands
```

---

## 3. 关键实现细节

### 3.1 角色分配

角色由 `p2p_connect()` 的 `remote_peer_id` 参数决定：

```c
// PUB（主动方）：知道对方的 gist_id
p2p_connect(handle, "abc123def456");

// SUB（被动方）：不知道对方，等待投递
p2p_connect(handle, NULL);
```

**职责分工：**

| 阶段 | SUB (被动方) | PUB (主动方) |
|------|-------------|-------------|
| **初始化** | 写入心跳时间戳到自己的 Gist，每 5分钟刷新 | 收集 ICE 候选（STUN 后台运行）|
| **发现** | 每 5s 轮询自己的 Gist | 读取 SUB Gist 时间戳，检查在线状态 → 投递 offer |
| **握手** | 收到 offer → 提取 PUB 的 gist_id | 每 1s 轮询 SUB Gist 等待响应 |
| **同步** | 发布候选到自己的 Gist；轮询 PUB Gist | 检测到候选 → 解密；发布自己候选到自己 Gist |
| **完成** | 获取 PUB 候选 → ICE 打洞 | 获取 SUB 候选 → ICE 打洞 |

### 3.2 轮询机制

两阶段轮询策略：

| 状态 | 轮询目标 | 间隔 | 说明 |
|------|---------|------|------|
| SUB 等待 offer | 自己的 Gist | 5s | 心跳模式，低频 |
| SUB 心跳刷新 | 自己的 Gist | 5min | 更新 ONLINE 时间戳 |
| PUB 等待响应 | SUB 的 Gist | 1s | 检测 SUB 是否收到 offer |
| 双方同步候选 | 对方的 Gist | 1s | 高频交换候选 |

```c
#define P2P_PUBSUB_POLL_MAILBOX_MS  5000   /* SUB 等待 offer */
#define P2P_PUBSUB_POLL_SYNC_MS     1000   /* 活跃同步 */
#define P2P_PUBSUB_HEARTBEAT_SEC    300    /* SUB 心跳刷新间隔（秒） */
```

**轮询逻辑：**
```
tick_recv:
  SUB/WAIT_OFFER: GET 自己的 Gist (5s)
    → 检测到 "OFFER:xxx" → 提取 remote_gist_id → 进入 SYNCING

  PUB/OFFERING: GET SUB 的 Gist (1s)
    → 仍是 "OFFER:" → SUB 还没响应，继续等待
    → 发现 "ONLINE:" → 心跳覆盖了 offer，重发 offer
    → 其他内容 → SUB 已响应候选数据 → 解密注入 → 进入 SYNCING

  SYNCING: GET 对方的 Gist (1s)
    → 解密候选 → 注入 remote_cands → nat_punch

tick_send:
  SUB/WAIT_OFFER: PATCH 自己的 Gist 写入 "ONLINE:<timestamp>"
                  首次写入 + 每 5 分钟刷新
  PUB/OFFERING:   GET SUB 的 Gist 检查时间戳 (>5min 则警告)
                  PATCH SUB 的 Gist 写入 offer
  SYNCING:        PATCH 自己的 Gist 写入候选
```

### 3.3 HTTP 请求处理

**读取 Gist（GET）：**
```bash
curl -s -H "Authorization: token ghp_xxx..." \
     -H "If-None-Match: \"etag_value\"" \
     https://api.github.com/gists/{id}
```

**更新 Gist（PATCH）：**
```bash
curl -s -X PATCH \
     -H "Authorization: token ghp_xxx..." \
     -H "Content-Type: application/json" \
     -d '{"files":{"p2p_signal.json":{"content":"..."}}}' \
     https://api.github.com/gists/{id}
```

**ETag 优化：**
- GitHub 返回 `ETag` 头用于缓存
- 使用 `If-None-Match` 避免重复读取
- HTTP 304 Not Modified = 无更新

---

## 4. 性能分析

### 4.1 延迟组成

**总连接时间拆解：**
```
SUB 启动 → 清空信箱 (t0)
    ↓ +5s (轮询等待 offer)
PUB 启动 → 投递 offer (t1)
    ↓ +1s (GitHub API)
SUB 检测到 offer，发布候选 (t2)
    ↓ +1s (PUB 轮询间隔)
PUB 检测到 SUB 候选，发布自己的候选 (t3)
    ↓ +1s (SUB 轮询间隔)
SUB 获取 PUB 候选 (t4)
    ↓ +2s (ICE 协商)
P2P 连接建立 (t5)
────────────────
总计: ~10 秒 (本地网络)
      ~15 秒 (跨网络 + NAT)
```

**对比传统 TCP 信令：**
```
TCP Server (WebSocket):
  Offer 发送: <100ms
  Answer 接收: <100ms
  ICE 协商: 2-5s
  总计: 3-6 秒

GitHub Gist:
  轮询延迟: 5-15s
  GitHub API: 1-2s
  ICE 协商: 2-5s
  总计: 10-25 秒
```

### 4.2 API 速率限制

**GitHub API 配额：**

| 认证状态 | 速率限制 | 适用场景 |
|---------|---------|---------|
| **未认证** | 60 请求/小时 | ❌ 不可用 |
| **Token 认证** | 5000 请求/小时 | ✅ 足够 |

**计算示例：**
```
轮询间隔: 5 秒
每小时轮询: 720 次
双向通信: 1440 次（Alice + Bob）
剩余配额: 3560 次（足够）
```

**速率限制检查：**
```bash
curl -H "Authorization: token $TOKEN" \
     https://api.github.com/rate_limit

# 响应:
{
  "rate": {
    "limit": 5000,
    "remaining": 4998,
    "reset": 1707823200,  // Unix timestamp
    "used": 2
  }
}
```

### 4.3 带宽占用

**单次信令数据量：**
```
Candidate Payload:
  - SDP 明文: ~80 bytes/候选 (a=candidate:... 行)
  - 3 个候选: ~240 bytes SDP
  - 加密后: ~248 bytes (8 字节对齐)
  - Base64: ~332 bytes
  - JSON 封装: ~350 bytes

完整 HTTP 请求:
  - Headers: ~500 bytes
  - Body: ~350 bytes
  - 总计: ~850 bytes

往返通信:
  - Offer + Answer: 1.5 KB
  - HTTP 响应: 2-5 KB
  - 总流量: <10 KB
```

---

## 5. 安全机制

### 5.1 加密层次

**多层防护：**
```
Layer 1: HTTPS 传输加密
    ↓
Layer 2: GitHub Token 认证
    ↓
Layer 3: DES 数据加密（内容）
    ↓
Layer 4: Secret Gist（URL 不可搜索）
```

### 5.2 密钥派生

```c
static void derive_key(const char *auth_key, uint8_t *key_out, size_t key_len) {
    memset(key_out, 0, key_len);
    if (auth_key && auth_key[0]) {
        size_t auth_len = strlen(auth_key);
        for (size_t i = 0; i < key_len && i < auth_len; i++) {
            key_out[i] = auth_key[i];
        }
    } else {
        /* 默认密钥 (不安全，仅用于测试) */
        memset(key_out, 0xAA, key_len);
    }
}
```

**改进建议（生产环境）：**
```c
// 使用 PBKDF2 派生密钥
PKCS5_PBKDF2_HMAC(
    auth_key, strlen(auth_key),
    salt, sizeof(salt),
    10000,  // 迭代次数
    EVP_sha256(),
    key_len, key_out
);
```

### 5.3 威胁模型

**假设攻击者能力：**
1. ❌ **无法** - 窃听 HTTPS 流量（TLS 保护）
2. ❌ **无法** - 破解 GitHub Token（256-bit 熵）
3. ⚠️ **可能** - 暴力搜索 Secret Gist URL（概率极低）
4. ⚠️ **可能** - 如果获得 Token，可读取 Gist（需 Token 泄露）

**防护措施：**
- 使用 Secret Gist（不可搜索）
- Token 最小权限（只授予 `gist`）
- 定期轮换 Token
- 数据内容加密（DES/AES）

---

## 6. 扩展性与限制

### 6.1 可扩展性

**并发连接数：**
```
理论上限: 无限（每对连接使用独立 Gist）
实际限制: GitHub API 速率限制 (5000/小时)

计算:
  单个连接建立: ~10 次 API 调用
  每小时可建立: 500 个新连接
  维持长连接: 无 API 消耗（P2P 直连）
```

**多房间支持：**
```
方案 1: 每个房间一个 Gist
  - Gist ID 作为房间标识
  - 简单隔离

方案 2: 单 Gist 多通道
  - JSON 键按房间分组
  - {"room1": {...}, "room2": {...}}
  - 节省 Gist 配额
```

### 6.2 适用场景

| 场景 | 适用性 | 原因 |
|------|-------|------|
| **个人项目** | ✅✅✅ | 完美匹配 |
| **原型验证** | ✅✅✅ | 快速迭代 |
| **小规模应用** | ✅✅ | <100 并发 |
| **文件传输工具** | ✅✅ | 非实时 |
| **实时游戏** | ❌ | 延迟太高 |
| **视频通话** | ⚠️ | 仅信令，媒体走 P2P |
| **大规模商用** | ❌ | 建议自建服务器 |

### 6.3 不适用场景

**明确不推荐：**
1. **高频率信令** - 超过 100 次/小时/连接
2. **超低延迟要求** - <1 秒连接建立
3. **大规模集群** - >1000 并发用户
4. **企业合规** - 依赖第三方服务可能违规

---

## 7. 对比分析

### 7.1 信令方案对比

| 特性 | TCP 服务器 | TURN 中继 | GitHub Gist | WebRTC Broker |
|------|-----------|----------|-------------|--------------|
| **成本** | 💰 中等 | 💰💰 高 | ✅ 免费 | 💰 低 |
| **延迟** | 快 (100ms) | 中 (500ms) | 慢 (5-15s) | 快 (200ms) |
| **复杂度** | 中 | 高 | ✅ 低 | 中 |
| **可靠性** | 取决于运维 | 高 | ✅ 99.9% | 中 |
| **扩展性** | 可扩展 | 受带宽限制 | ⚠️ API 限制 | 好 |
| **NAT 穿透** | ✅ 是 | ✅ 是 | ✅ 是 | ✅ 是 |

### 7.2 技术创新点

**本项目独特之处：**

1. **无服务器架构先驱**
   - 首个使用 GitHub Gist 作为 P2P 信令的 C 语言实现
   - 展示低成本架构可能性

2. **轻量级设计**
   - 无需数据库
   - 无需消息队列
   - 单一 API 端点

3. **教育价值**
   - 清晰展示 P2P 信令原理
   - 可作为学习范例

---

## 8. 未来优化方向

### 8.1 短期改进

1. **减少轮询延迟**
   ```c
   // 从 5s 减少到 3s
   int poll_interval = 3000;
   ```

2. **实现 ETag 缓存**
   ```c
   // 避免重复读取未更改内容
   char last_etag[64];
   snprintf(headers, "If-None-Match: \"%s\"", last_etag);
   ```

3. **使用 libcurl 替代 system()**
   ```c
   CURL *curl = curl_easy_init();
   curl_easy_setopt(curl, CURLOPT_URL, api_url);
   // ... 更安全、更高效
   ```

### 8.2 高级特性

1. **WebHook 集成**
   - 使用 Zapier/IFTTT 监听 Gist 更新
   - 推送通知到客户端
   - 消除轮询延迟

2. **混合模式**
   ```
   优先级:
   1. 尝试 WebSocket 服务器 (如果可用)
   2. 降级到 GitHub Gist
   3. 最终回退到 TURN 中继
   ```

3. **多云支持**
   - GitLab Snippets
   - Bitbucket Snippets
   - Pastebin API

---

## 9. 总结

### 核心价值

GitHub Gist 信令方案通过巧妙利用现有云服务，证明了：
- ✅ P2P 应用不一定需要专用服务器
- ✅ 低成本架构也能实现可靠通信
- ✅ 轻量级方案适合个人和小团队

### 关键认知

1. **不是银弹** - 存在延迟和速率限制
2. **场景适配** - 适合非实时、低频连接
3. **架构启发** - 展示无服务器设计思路

### 最佳实践

- 用于原型验证和快速迭代
- 生产环境结合自建服务器
- 作为备用信令通道增强可靠性

---

**相关文档：**
- [GIST_DEPLOYMENT_GUIDE.md](GIST_DEPLOYMENT_GUIDE.md) - 部署和使用指南
- [ARCHITECTURE.md](ARCHITECTURE.md) - 系统架构总览
- [SECURITY.md](SECURITY.md) - 安全最佳实践
