# p2p0

🎯 **零依赖 P2P 通信库，纯 C99 实现**

✨ **创新特性：GitHub Gist 信令** - 无需自建服务器即可完成 P2P 握手

📡 **多信令协议：SIMPLE (UDP) / ICE-RELAY (TCP) / PUBSUB (Gist)**

🔧 **含完整信令服务器实现**

---

## 特性

- ✅ **零依赖**：纯 C99 标准库实现，无需第三方依赖
- ✅ **跨平台**：支持 Linux、macOS、Windows
- ✅ **多协议**：三种信令协议可选
  - **SIMPLE**: UDP 直接信令，低延迟
  - **ICE-RELAY**: TCP 中继信令，支持 NAT 穿透
  - **PUBSUB**: GitHub Gist 信令，真正零服务器
- ✅ **完整示例**：包含服务器和客户端完整实现
- ✅ **简单易用**：清晰的 API 设计

## 快速开始

### 编译

```bash
make
```

这将构建静态库和所有示例程序。

### SIMPLE 协议示例

**终端 1 - 启动信令服务器：**
```bash
./bin/server_simple
```

**终端 2 - 启动监听节点：**
```bash
./bin/client_simple listen peer1
```

**终端 3 - 启动连接节点：**
```bash
./bin/client_simple connect peer2
```

### ICE-RELAY 协议示例

**终端 1 - 启动中继服务器：**
```bash
./bin/server_ice_relay
```

**终端 2 - 发送 Offer：**
```bash
./bin/client_ice_relay offer session123
```

**终端 3 - 发送 Answer：**
```bash
./bin/client_ice_relay answer session123
```

### PUBSUB 协议示例

**发布节点信息到 Gist：**
```bash
./bin/client_pubsub publish peer1 <gist_id> <github_token>
```

**订阅节点信息：**
```bash
./bin/client_pubsub subscribe peer2 <gist_id>
```

## API 文档

### 核心 API

```c
#include "p2p0.h"

// 初始化 P2P 上下文
int p2p0_init(p2p0_ctx_t *ctx);

// 创建 socket 并绑定本地地址
int p2p0_create_socket(p2p0_ctx_t *ctx, uint16_t port);

// 连接到远程节点
int p2p0_connect(p2p0_ctx_t *ctx, const char *remote_address, uint16_t remote_port);

// 发送数据
int p2p0_send(p2p0_ctx_t *ctx, const void *data, size_t len);

// 接收数据
int p2p0_recv(p2p0_ctx_t *ctx, void *buffer, size_t len);

// 关闭连接
void p2p0_close(p2p0_ctx_t *ctx);
```

### SIMPLE 协议 API

```c
#include "p2p0_simple.h"

// 初始化 SIMPLE 协议
int p2p0_simple_init(p2p0_ctx_t *ctx, const char *server_address, 
                     uint16_t server_port, const char *peer_id);

// 注册到信令服务器
int p2p0_simple_register(p2p0_ctx_t *ctx);

// 获取节点信息
int p2p0_simple_get_peer(p2p0_ctx_t *ctx, const char *peer_id, p2p0_peer_t *peer);

// 建立 P2P 连接
int p2p0_simple_connect(p2p0_ctx_t *ctx, const char *peer_id);

// 清理资源
void p2p0_simple_cleanup(p2p0_ctx_t *ctx);
```

### ICE-RELAY 协议 API

```c
#include "p2p0_ice_relay.h"

// 初始化 ICE-RELAY 协议
int p2p0_ice_relay_init(p2p0_ctx_t *ctx, const char *server_address,
                        uint16_t server_port, const char *session_id);

// 添加 ICE 候选
int p2p0_ice_relay_add_candidate(p2p0_ctx_t *ctx, const char *address,
                                  uint16_t port, uint8_t priority);

// 发送 Offer
int p2p0_ice_relay_send_offer(p2p0_ctx_t *ctx);

// 接收 Answer
int p2p0_ice_relay_receive_answer(p2p0_ctx_t *ctx);

// 建立连接
int p2p0_ice_relay_connect(p2p0_ctx_t *ctx);

// 清理资源
void p2p0_ice_relay_cleanup(p2p0_ctx_t *ctx);
```

### PUBSUB 协议 API

```c
#include "p2p0_pubsub.h"

// 初始化 PUBSUB 协议
int p2p0_pubsub_init(p2p0_ctx_t *ctx, const char *gist_id,
                     const char *github_token, const char *peer_id);

// 发布节点信息
int p2p0_pubsub_publish(p2p0_ctx_t *ctx);

// 订阅节点信息
int p2p0_pubsub_subscribe(p2p0_ctx_t *ctx, const char *peer_id, p2p0_peer_t *peer);

// 建立连接
int p2p0_pubsub_connect(p2p0_ctx_t *ctx, const char *peer_id);

// 清理资源
void p2p0_pubsub_cleanup(p2p0_ctx_t *ctx);
```

## 架构设计

```
┌─────────────────────────────────────────────────────────┐
│                   应用层 (Application)                   │
├─────────────────────────────────────────────────────────┤
│              P2P0 核心 API (p2p0.h/c)                   │
│         - 连接管理                                       │
│         - 数据传输                                       │
│         - 状态机                                         │
├──────────────┬──────────────┬──────────────────────────┤
│  SIMPLE      │  ICE-RELAY   │     PUBSUB               │
│  (UDP)       │  (TCP)       │  (GitHub Gist)           │
│              │              │                           │
│ p2p0_simple  │ p2p0_ice_    │  p2p0_pubsub             │
│              │ relay        │                           │
├──────────────┴──────────────┴──────────────────────────┤
│          系统 Socket API (跨平台抽象)                    │
└─────────────────────────────────────────────────────────┘
```

## 协议说明

### SIMPLE 协议 (UDP)

最简单的信令协议，使用 UDP 进行快速的节点发现和信息交换。

**优点：**
- 低延迟
- 简单高效
- 适合局域网

**缺点：**
- 需要独立的信令服务器
- UDP 可能被防火墙阻止

### ICE-RELAY 协议 (TCP)

基于 TCP 的可靠信令协议，支持 ICE 候选交换和中继。

**优点：**
- 可靠传输
- 支持 NAT 穿透
- 可以使用中继服务器

**缺点：**
- 需要 TCP 信令服务器
- 延迟稍高

### PUBSUB 协议 (GitHub Gist)

创新的无服务器信令方案，使用 GitHub Gist 作为信令通道。

**优点：**
- 真正的零服务器部署
- 无需维护基础设施
- 适合小规模应用和演示

**缺点：**
- 需要 GitHub 账号和 API token
- 轮询延迟较高
- 受 GitHub API 速率限制

## 项目结构

```
p2p0/
├── include/              # 头文件
│   ├── p2p0.h           # 核心 API
│   ├── p2p0_simple.h    # SIMPLE 协议
│   ├── p2p0_ice_relay.h # ICE-RELAY 协议
│   └── p2p0_pubsub.h    # PUBSUB 协议
├── src/                 # 源代码
│   ├── p2p0.c
│   ├── p2p0_simple.c
│   ├── p2p0_ice_relay.c
│   └── p2p0_pubsub.c
├── examples/            # 示例程序
│   ├── client_simple.c
│   ├── client_ice_relay.c
│   ├── client_pubsub.c
│   ├── server_simple.c
│   └── server_ice_relay.c
├── docs/                # 文档
├── Makefile            # 构建脚本
└── README.md           # 本文件
```

## 编译选项

```bash
# 编译全部
make

# 清理
make clean

# 编译并测试
make test

# 查看帮助
make help
```

## 系统要求

- **编译器**: GCC, Clang, MSVC (支持 C99)
- **操作系统**: Linux, macOS, Windows
- **依赖**: 无 (仅使用标准 C 库和系统 socket API)

## 贡献

欢迎提交 Issue 和 Pull Request！

## 许可证

MIT License

## 作者

p2p0 项目团队

---

⭐ 如果这个项目对您有帮助，请给我们一个 Star！
