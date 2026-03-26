# TCP 粘包问题修复文档

## 问题描述

RELAY 信令服务器（基于 TCP）存在严重的粘包/半包处理缺陷：
- 假设每次 `recv()` 调用都能接收完整的协议包
- 没有处理 TCP 流式传输的特性（数据可能被拆分或合并）
- 可能导致协议解析错误、崩溃或数据混乱

## TCP 粘包/半包问题

### 粘包（Packet Sticking）
一次 `recv()` 可能接收多个完整的协议包：
```
[Packet1][Packet2][Packet3] → recv() 一次性全部接收
```

### 半包（Packet Fragmentation）
一个完整的协议包可能需要多次 `recv()` 才能接收完整：
```
[Pack|    → recv() 第1次
     |et] → recv() 第2次
```

### 复合情况
```
[Packet1][Pac|    → recv() 第1次接收完整包+半包
            |ket2][Packet3... → recv() 第2次接收半包尾+完整包+半包头
```

## 解决方案设计

### 1. 为每个客户端添加接收缓冲区

修改 `relay_client_t` 结构体：
```c
typedef struct relay_client {
    // ... 原有字段 ...
    
    // TCP 接收缓冲区（处理粘包/半包）
    uint8_t    recv_buf[65536];      // 接收缓冲区
    uint32_t   recv_len;             // 当前缓冲区数据长度
    uint32_t   expected_pkt_len;     // 期望的完整包长度（header 解析后得到）
    
    // ... 其他字段 ...
} relay_client_t;
```

### 2. 实现接收状态机

创建 `handle_relay_signaling()` 函数实现状态机：

```c
while (1) {
    // 步骤1: 检查是否有完整的 header（9 bytes）
    if (recv_len < sizeof(p2p_relay_hdr_t)) break;
    
    // 步骤2: 解析 header 获取完整包长度
    p2p_relay_hdr_t *hdr = (p2p_relay_hdr_t *)recv_buf;
    uint32_t full_pkt_len = sizeof(p2p_relay_hdr_t) + hdr->length;
    
    // 步骤3: 检查是否接收完整包
    if (recv_len < full_pkt_len) break;  // 等待更多数据（半包）
    
    // 步骤4: 有完整包，调用处理函数
    process_relay_packet(idx, recv_buf, full_pkt_len);
    
    // 步骤5: 从缓冲区移除已处理的包（处理粘包）
    remaining = recv_len - full_pkt_len;
    if (remaining > 0) {
        memmove(recv_buf, recv_buf + full_pkt_len, remaining);
    }
    recv_len = remaining;
}
```

### 3. 分离协议包处理逻辑

创建 `process_relay_packet()` 函数处理单个完整协议包：
- 输入：完整的协议包数据（header + payload）
- 直接从内存读取，避免 `recv()` 调用
- 所有协议处理逻辑从 `handle_relay_signaling()` 迁移到此函数

## 代码修改详情

### 文件：p2p_server/server.c

#### 1. 结构体修改（Line ~88-110）
添加三个新字段：
- `recv_buf[65536]`: 接收缓冲区
- `recv_len`: 当前数据长度  
- `expected_pkt_len`: 期望包长度

#### 2. 新增函数（Line ~206-308）
- `handle_relay_signaling()`: TCP 接收状态机
  * 累积接收数据
  * 循环处理所有完整包
  * 处理粘包/半包
  * 验证 magic 和包长度

- `process_relay_packet()`: 协议包处理
  * P2P_RLY_LOGIN: 登录处理
  * P2P_RLY_CONNECT/ANSWER: 信令转发
  * P2P_RLY_LIST: 在线用户列表
  * P2P_RLY_HEARTBEAT: 心跳

#### 3. 数据读取修改
**之前（错误）：**
```c
recv(fd, &login, sizeof(login), 0);  // 假设能完整接收
```

**现在（正确）：**
```c
memcpy(&login, payload, sizeof(login));  // 从已接收的缓冲区读取
```

#### 4. 初始化修改
在以下位置添加缓冲区初始化：
- Line ~2340: 接受新 TCP 连接时
  ```c
  g_relay_clients[i].recv_len = 0;
  g_relay_clients[i].expected_pkt_len = 0;
  ```
  
- Line ~552: 创建离线用户槽位时
  ```c
  g_relay_clients[i].recv_len = 0;
  g_relay_clients[i].expected_pkt_len = 0;
  ```

#### 5. 清理修改（Line ~695）
添加缓冲区状态清理：
```c
g_relay_clients[i].recv_len = 0;
g_relay_clients[i].expected_pkt_len = 0;
```

## 关键特性

### 1. 完整的粘包处理
- 循环处理缓冲区中的所有完整包
- 使用 `memmove()` 保留未处理的剩余数据

### 2. 完整的半包处理
- 累积数据直到有完整包
- 跨多次 `recv()` 调用保持状态

### 3. 安全性增强
- 验证 magic 防止协议错误
- 检查包长度防止缓冲区溢出（最大 65536）
- 缓冲区满时断开连接

### 4. 性能优化
- 使用 `memcpy()` 代替多次 `recv()` 
- 减少系统调用次数
- 内存操作更高效

## 测试场景

### 场景1：正常包
```
recv() → [完整包] → 立即处理
```

### 场景2：半包
```
recv1() → [包头|   ] → 等待
recv2() → [   |包尾] → 组合后处理
```

### 场景3：粘包
```
recv() → [包1][包2][包3] → 循环处理3个包
```

### 场景4：半包+粘包
```
recv1() → [包1][包2头| ] → 处理包1，保留包2头
recv2() → [    |包2尾][包3] → 组合处理包2，然后处理包3
```

### 场景5：恶意超大包
```
recv() → [magic][type][length=999999...] → 检测到超大包，断开连接
```

## 兼容性

### 不影响现有功能
- 所有协议处理逻辑保持不变
- 只是改变数据读取方式
- 客户端无需修改

### 向后兼容
- 旧客户端仍然可以正常工作
- 渐进式修复，不破坏现有连接

## 编译与测试

### 编译
```bash
cd build_cmake
make p2p_server
```

### 测试建议
1. **正常通信测试**：启动服务器和多个客户端，测试正常信令交换
2. **高负载测试**：大量客户端同时连接和发送数据
3. **网络模拟**：使用工具模拟网络延迟、丢包、带宽限制
4. **压力测试**：发送大量小包或超大包
5. **异常测试**：发送错误的 magic、超大 length、不完整的包

### 验证点
- ✅ 服务器不崩溃
- ✅ 所有协议包正确解析
- ✅ 粘包正确分离
- ✅ 半包正确组合
- ✅ 错误包被正确拒绝
- ✅ 内存无泄漏
- ✅ 连接正常断开

## 技术要点

### 为什么使用 memmove() 而不是 memcpy()？
`memmove()` 支持内存区域重叠，当处理完一个包后，需要将剩余数据移到缓冲区开头：
```c
memmove(recv_buf, recv_buf + processed_len, remaining);
```
源和目标内存区域重叠，必须使用 `memmove()`。

### 为什么缓冲区大小是 65536？
- 匹配最大 payload 检查
- 足够容纳最大的合法协议包
- 防止内存占用过大（每个客户端 64KB）

### 如何处理缓冲区满？
如果缓冲区满且仍无完整包：
- 说明包过大或协议错误
- 断开连接并清理资源
- 记录错误日志

## 总结

本次修复彻底解决了 RELAY 信令服务器的 TCP 粘包/半包问题：
- ✅ 完整的接收缓冲区管理
- ✅ 状态机处理粘包/半包
- ✅ 安全边界检查
- ✅ 向后兼容
- ✅ 性能优化

这是一个**生产级**的修复方案，遵循 TCP 协议最佳实践。
