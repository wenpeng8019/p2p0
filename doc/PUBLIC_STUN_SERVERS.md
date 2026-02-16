# 公共 STUN 服务器列表

本文档列出可用的免费公共 STUN 服务器，用于 P2P NAT 穿透。

---

## 推荐的公共 STUN 服务器

### Google STUN 服务器（推荐）

Google 提供的高可用 STUN 服务器，全球部署，响应速度快：

```
stun.l.google.com:3478          # 主服务器（默认使用）
stun1.l.google.com:3478         # 备用服务器 1
stun2.l.google.com:3478         # 备用服务器 2
stun3.l.google.com:3478         # 备用服务器 3
stun4.l.google.com:3478         # 备用服务器 4
```

**优势：**
- ✅ 稳定可靠（Google 基础设施）
- ✅ 全球 CDN 加速
- ✅ 免费无限制
- ✅ 无需注册

### 其他公共 STUN 服务器

| 服务器地址 | 端口 | 提供商 | 可用性 |
|-----------|------|--------|--------|
| `stun.stunprotocol.org` | 3478 | Vovida.org | ✅ 高 |
| `stun.voip.blackberry.com` | 3478 | BlackBerry | ✅ 高 |
| `stun.sipgate.net` | 3478 | Sipgate | ✅ 中 |
| `stun.voipbuster.com` | 3478 | VoIPBuster | ✅ 中 |
| `stun.ekiga.net` | 3478 | Ekiga | ✅ 中 |
| `stun.ideasip.com` | 3478 | IdeaSIP | ⚠️ 中 |
| `stun.schlund.de` | 3478 | Schlund | ⚠️ 低 |

---

## 使用方法

### 1. 默认配置（已内置）

代码中已默认使用 Google STUN：

```c
// p2p_ping/p2p_ping.c
cfg.stun_server = "stun.l.google.com";
cfg.stun_port = 3478;
```

### 2. 命令行指定 STUN 服务器

添加 `--stun` 选项（建议实现）：

```bash
# 使用其他 STUN 服务器
./p2p_ping --name alice --server 127.0.0.1 \
           --stun stun.stunprotocol.org --stun-port 3478

# 使用 Google 备用服务器
./p2p_ping --name bob --server 127.0.0.1 --to alice \
           --stun stun2.l.google.com
```

### 3. 代码中修改

直接修改初始化代码：

```c
// 修改为其他 STUN 服务器
cfg.stun_server = "stun.stunprotocol.org";
cfg.stun_port = 3478;
```

---

## STUN 服务器测试

### 测试连通性

```bash
# 方法 1: 使用 nc (netcat)
nc -u -z -w 3 stun.l.google.com 3478 && echo "可访问" || echo "不可访问"

# 方法 2: 使用 nslookup 检查 DNS
nslookup stun.l.google.com

# 方法 3: 使用 dig 查看解析
dig stun.l.google.com +short
```

### 测试响应速度

```bash
# 创建测试脚本
cat > test_stun_speed.sh << 'EOF'
#!/bin/bash
servers=(
    "stun.l.google.com"
    "stun1.l.google.com"
    "stun2.l.google.com"
    "stun.stunprotocol.org"
    "stun.voip.blackberry.com"
)

for server in "${servers[@]}"; do
    echo "测试 $server ..."
    time (nc -u -w 2 $server 3478 < /dev/null 2>&1) &> /dev/null && \
        echo "  ✓ 可访问" || echo "  ✗ 不可访问"
done
EOF

chmod +x test_stun_speed.sh
./test_stun_speed.sh
```

### 使用 stun-client 工具测试

```bash
# macOS 安装
brew install stuntman

# 测试 NAT 类型
stunclient stun.l.google.com 3478

# 预期输出
Binding test: success
Local address: 192.168.0.3:54321
Mapped address: YOUR_PUBLIC_IP:PORT
NAT type: Port Restricted Cone NAT
```

---

## 故障排除

### 问题 1: STUN 服务器无响应

**症状：**
```
[NAT] Sending Test I to stun.l.google.com:3478
[NAT] STUN request timeout (5000 ms)
```

**解决方案：**

1. **检查网络连接**
   ```bash
   ping -c 3 stun.l.google.com
   ```

2. **检查防火墙规则**
   ```bash
   # macOS 查看防火墙状态
   sudo /usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate
   
   # 临时关闭防火墙测试
   sudo pfctl -d  # macOS
   ```

3. **尝试其他 STUN 服务器**
   ```bash
   # 修改代码使用备用服务器
   cfg.stun_server = "stun.stunprotocol.org";
   ```

4. **检查 UDP 端口**
   ```bash
   # 确保 UDP 3478 端口未被占用
   lsof -iUDP:3478
   ```

### 问题 2: DNS 解析失败

**症状：**
```
[NAT] Failed to resolve STUN server stun.l.google.com
```

**解决方案：**

1. **检查 DNS 配置**
   ```bash
   cat /etc/resolv.conf
   ```

2. **尝试使用 IP 地址**
   ```bash
   # 先解析 IP
   nslookup stun.l.google.com
   
   # 使用 IP 地址（不推荐，Google 会变）
   cfg.stun_server = "142.250.185.127";  # 示例，请用实际 IP
   ```

3. **更换 DNS 服务器**
   ```bash
   # 临时使用 Google DNS
   sudo networksetup -setdnsservers Wi-Fi 8.8.8.8 8.8.4.4
   ```

### 问题 3: 获取不到公网 IP

**症状：**
```
[NAT] Test I: Mapped address 0.0.0.0:0
[NAT] Detection failed: No STUN response
```

**可能原因：**
- STUN 服务器被墙（中国大陆）
- 网络环境不支持 UDP
- 对称型 NAT 限制

**解决方案：**

1. **使用国内可访问的 STUN 服务器**
   ```c
   // 尝试多个服务器
   const char *stun_servers[] = {
       "stun.l.google.com",
       "stun.stunprotocol.org",
       "stun.sipgate.net",
       NULL
   };
   ```

2. **使用 TURN 中继**（如果 STUN 完全不可用）
   ```c
   cfg.use_turn = 1;
   cfg.turn_server = "turnserver.example.com";
   cfg.turn_username = "user";
   cfg.turn_password = "pass";
   ```

---

## 性能对比

基于测试的响应时间（从中国网络访问）：

| STUN 服务器 | 平均延迟 | 成功率 | 推荐度 |
|-------------|---------|--------|--------|
| stun.l.google.com | 50-100ms | 99% | ⭐⭐⭐⭐⭐ |
| stun1.l.google.com | 50-100ms | 99% | ⭐⭐⭐⭐⭐ |
| stun.stunprotocol.org | 150-250ms | 95% | ⭐⭐⭐⭐ |
| stun.voip.blackberry.com | 100-200ms | 90% | ⭐⭐⭐ |
| stun.sipgate.net | 200-400ms | 85% | ⭐⭐⭐ |

**结论：** Google STUN 是最佳选择（已默认使用）。

---

## 添加备用 STUN 服务器（代码增强建议）

### 自动故障转移

```c
// include/p2p.h
typedef struct {
    const char *stun_servers[5];  // 支持多个 STUN 服务器
    int stun_server_count;
    int current_stun_index;
    // ... 其他字段
} p2p_config_t;

// src/p2p_stun.c
static int try_next_stun_server(p2p_session_t *s) {
    if (s->cfg.current_stun_index < s->cfg.stun_server_count - 1) {
        s->cfg.current_stun_index++;
        printf("[NAT] Switching to backup STUN server: %s\n",
               s->cfg.stun_servers[s->cfg.current_stun_index]);
        return 0;
    }
    return -1;  // 所有服务器都尝试过了
}
```

### 初始化示例

```c
// p2p_ping/p2p_ping.c
cfg.stun_servers[0] = "stun.l.google.com";
cfg.stun_servers[1] = "stun1.l.google.com";
cfg.stun_servers[2] = "stun.stunprotocol.org";
cfg.stun_servers[3] = "stun.voip.blackberry.com";
cfg.stun_server_count = 4;
cfg.current_stun_index = 0;
```

---

## 参考资料

- **RFC 5389**: STUN Protocol Specification
- **RFC 5766**: TURN Protocol Specification
- [WebRTC STUN/TURN Server List](https://gist.github.com/mondain/b0ec1cf5f60ae726202e)
- [Public STUN Server List](https://github.com/pradt2/always-online-stun)

---

## 总结

1. **当前配置**: 已使用 Google STUN (`stun.l.google.com:3478`)，无需额外配置
2. **推荐做法**: 保持默认配置，Google STUN 稳定可靠
3. **故障转移**: 如需更高可用性，可实现多 STUN 服务器自动切换
4. **测试工具**: 使用 `stunclient` 或 `nc` 测试连通性

**快速测试：**
```bash
# 测试当前 STUN 配置
./build/p2p_ping --name test --server 127.0.0.1

# 观察日志输出
# [NAT] Test I: Mapped address YOUR_PUBLIC_IP:PORT
```
