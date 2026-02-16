# P2P Signaling Server

本项目提供了一个轻量级的信令服务器（Signaling Server），用于在 Peer 之间交换元数据（如 ICE Candidates），辅助建立 P2P 连接。

## 支持的信令模式

| 模式 | 传输层 | 客户端模块 | 说明 |
| :--- | :--- | :--- | :--- |
| **RELAY** | TCP 长连接 | `p2p_signal_relay` | 登录、用户列表、候选地址转发 |
| **SIMPLE** | UDP 无状态 | `p2p_trans_simple` | 简单的地址交换，适合受控环境 |

## 1. 核心架构与角色

在 P2P 通信中，本服务器与其他组件的协作关系如下：

| 组件 | 角色 | 职责 | 备注 |
| :--- | :--- | :--- | :--- |
| **`p2p_server`** | **信令服务器** | 身份注册、在线列表管理、信令包中转 | **必须自主开发**，承载业务逻辑 |
| **STUN Server** | 辅助工具 | 探测公网 IP/Port，辅助 NAT 打洞 | 可使用 `coturn` 或公共服务（如 Google） |
| **TURN Server** | 转发中继 | 极端网络下的加密流量中转（保底方案） | 推荐使用 `coturn`，实现高性能转发 |

## 2. 运行场景模式

### 场景 A：独立运行 `p2p_server` (基础模式)
*   **适用**：Peer 均在公网，或在同一局域网内。
*   **流程**：Peer 登录服务器 -> 交换内网 IP -> 建立 P2P 直连。

### 场景 B：`p2p_server` + 公共 STUN (推荐模式)
*   **适用**：跨公网 P2P，大部分常规家庭/办公网络。
*   **流程**：
    1. Peer 通过公共 STUN（如 `stun.l.google.com`）获取自己的公网映射。
    2. 通过 `p2p_server` 交换这些映射地址。
    3. 双方进行打洞建立直连。

### 场景 C：`p2p_server` + 私有 `coturn` (企业级模式)
*   **适用**：严格的防火墙（Symmetric NAT）环境。
*   **流程**：当打洞失败时，库会自动通过配置好的 `coturn` 服务器通过 TURN 协议中转流量。

## 3. 构建与启动

### 构建
```bash
# 在 build 目录下
cmake ..
make p2p_server
```

### 启动
```bash
./p2p_server/p2p_server [端口, 默认 8888]
```

## 4. 协议简介 (protocol.h)
采用简单的二进制协议：
`[Magic: 4 bytes] [Type: 1 byte] [Payload Length: 4 bytes] [Payload: N bytes]`

支持的消息类型：
- `MSG_LOGIN`: 注册 Peer ID。
- `MSG_LIST`: 获取当前在线列表。
- `MSG_SIGNAL`: 透传信令数据。
