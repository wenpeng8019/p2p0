# 第三方依赖说明 (Third-Party Dependencies)

本项目在 `third_party` 目录下包含了一些集成好的第三方库。

本地集成（Vendoring）这些库是为了确保 **“零依赖”** 的构建体验，即项目可以在一个没有安装复杂包管理器或系统级库的新系统中直接编译运行。

## 已集成的库

### 1. MbedTLS
- **目录**: `third_party/mbedtls`
- **版本**: 2.28.10 LTS
- **用途**: 为 P2P 通信提供 DTLS (数据报传输层安全) 支持，实现端到端加密。
- **为什么选择 2.28 版本？**:
    - **稳定性**: 2.28 是一个长期支持 (LTS) 分支，拥有非常稳定的 API，不会频繁变动。
    - **构建简单**: 与 MbedTLS 3.x 不同，2.28 版本不需要 Python 或复杂的头文件生成脚本。它可以使用简单的 `Makefile` 直接编译，非常符合本项目“零依赖”的目标。
- **安全注意事项**:
    - MbedTLS 2.28 官方对安全补丁的支持已于 **2024年底** 正式结束。
    - **警告**: 对于需要最新安全补丁和 TLS 1.3 支持的生产系统，建议未来升级到 **MbedTLS 3.6.x LTS**。
    - 当前的集成主要作为模块化传输架构的功能性验证（Proof-of-Concept）。

### 2. usrsctp
- **目录**: `third_party/usrsctp`
- **版本**: 0.9.5.0
- **用途**: 提供 SCTP (流控制传输协议) 支持，实现在 UDP/DTLS 之上的多流复用。
- **集成方式**: 可选，通过 `WITH_SCTP=ON` 启用（默认关闭）。CMake 以子目录方式编译并链接。
- **使用位置**: `src/p2p_trans_sctp.c`，运行时通过 `cfg.use_sctp` 选择。

### 3. wslay
- **目录**: `third_party/wslay`
- **版本**: 1.1.1
- **用途**: WebSocket 协议库，用于信令通道的 WebSocket 通信。
- **集成方式**: 默认启用（`WITH_WSLAY=ON`）。直接嵌入源文件编译（event, frame, net, queue, stack），避免上游 `-Werror` 导致的编译问题。
- **使用位置**:
    - 客户端: `src/ws_client.c` — WebSocket 客户端，连接信令服务器。
    - 服务端: `p2p_server/ws_server.c` — WebSocket 服务器，接受 peer 信令连接。

## 如何更新
若要更新库版本：
1. 用新版本的源码替换对应子目录的内容。
2. 如果库的目录结构发生变化，需同步更新根目录 `Makefile` 中的 `include` 路径。
3. 确保静态库文件名（`.a` 文件）与链接器标志匹配。
