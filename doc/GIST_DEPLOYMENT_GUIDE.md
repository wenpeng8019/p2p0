# GitHub Gist 信令部署指南

**快速部署无服务器 P2P 信令通道**

---

## 1. 前置准备

### 1.1 系统要求

**操作系统：**
- macOS 10.13+
- Linux (Ubuntu 18.04+, CentOS 7+)
- Windows 10+ (WSL2)

**依赖工具：**
```bash
# 验证工具安装
curl --version      # HTTP 客户端
jq --version        # JSON 处理器（可选）
git --version       # 版本控制

# macOS
brew install curl jq

# Ubuntu/Debian
sudo apt install curl jq

# CentOS/RHEL
sudo yum install curl jq
```

**编译环境：**
```bash
gcc --version       # >= 4.9
cmake --version     # >= 3.10
make --version      # GNU Make
```

### 1.2 GitHub 账号准备

**注册账号：**
```
https://github.com/signup
```

**启用 2FA（推荐）：**
```
Settings → Password and authentication → Two-factor authentication
```

---

## 2. GitHub 配置

### 2.1 创建 Personal Access Token

**步骤详解：**

1. **访问 Token 管理页面**
   ```
   https://github.com/settings/tokens
   ```
   
   或导航：
   ```
   右上角头像 → Settings → Developer settings → 
   Personal access tokens → Tokens (classic)
   ```

2. **点击 "Generate new token (classic)"**

3. **配置 Token 参数**
   ```
   Note: P2P Signaling Channel
   Expiration: 
     ○ 7 days       (短期测试)
     ○ 30 days      (开发阶段)
     ◉ 90 days      (推荐)
     ○ No expiration (不推荐，安全风险)
   
   Select scopes:
     ☑ gist  ← 只勾选这一个！
       Create gists
   ```

4. **生成并保存 Token**
   ```bash
   # Token 格式
   ghp_1Ab2Cd3Ef4Gh5Ij6Kl7Mn8Op9Qr0St1Uv2Wx3Yz
   └┬┘ └──────────────────────┬──────────────────────┘
   前缀        40 个字符（Base62）
   
   # 立即保存到安全位置！
   # 离开页面后无法再次查看
   ```

5. **验证 Token 有效性**
   ```bash
   export TOKEN="ghp_xxx..."
   
   curl -H "Authorization: token $TOKEN" \
        https://api.github.com/user
   
   # 预期输出: 包含 "login": "your-username"
   ```

**Token 权限说明：**

| 权限 | 说明 | 必需？ |
|------|------|-------|
| `gist` | 创建和修改 Gist | ✅ 是 |
| `repo` | 访问仓库 | ❌ 否 |
| `user` | 读取用户信息 | ❌ 否 |

⚠️ **安全提醒：** 只授予 `gist` 权限，遵循最小权限原则

### 2.2 创建信令 Gist

**方法 1: Web 界面（推荐首次使用）**

1. **访问 Gist 首页**
   ```
   https://gist.github.com/
   ```

2. **填写 Gist 信息**
   ```
   Gist description: P2P Signaling Channel - DO NOT EDIT
   
   Filename including extension: p2p_signal.json
   
   File contents:
   {}
   ```
   
   ⚠️ 重要：必须是合法的空 JSON 对象 `{}`

3. **选择可见性**
   ```
   ◉ Create secret gist  ← 推荐！
   ○ Create public gist  ← 不推荐（可被搜索）
   ```

4. **点击 "Create secret gist"**

5. **复制 Gist ID**
   
   URL 示例：
   ```
   https://gist.github.com/username/1d3ee11b4bcdfd6ff16c888c6bcff3d6
                                    └──────────┬──────────────────┘
                                            Gist ID (32个十六进制字符)
   ```
   
   复制这部分：`1d3ee11b4bcdfd6ff16c888c6bcff3d6`

**方法 2: API 命令行创建**

```bash
# 设置 Token
export TOKEN="ghp_xxx..."

# 创建 Gist
curl -X POST \
  -H "Authorization: token $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "description": "P2P Signaling Channel",
    "public": false,
    "files": {
      "p2p_signal.json": {
        "content": "{}"
      }
    }
  }' \
  https://api.github.com/gists

# 从响应中提取 Gist ID
# 响应 JSON 的 "id" 字段即为 Gist ID
```

**方法 3: 使用辅助脚本**

```bash
# 创建脚本
cat > create_gist.sh << 'EOF'
#!/bin/bash
TOKEN="$1"
DESCRIPTION="${2:-P2P Signaling Channel}"

response=$(curl -s -X POST \
  -H "Authorization: token $TOKEN" \
  -H "Content-Type: application/json" \
  -d "{
    \"description\": \"$DESCRIPTION\",
    \"public\": false,
    \"files\": {
      \"p2p_signal.json\": {
        \"content\": \"{}\"
      }
    }
  }" \
  https://api.github.com/gists)

gist_id=$(echo "$response" | jq -r '.id')
echo "✅ Gist 创建成功！"
echo "Gist ID: $gist_id"
echo "URL: https://gist.github.com/$(echo "$response" | jq -r '.owner.login')/$gist_id"
EOF

chmod +x create_gist.sh

# 使用
./create_gist.sh "ghp_xxx..."
```

---

## 3. 本地环境配置

### 3.1 环境变量设置

**Linux/macOS (Bash/Zsh):**

```bash
# 方法 1: 添加到 shell 配置文件（持久化）
echo 'export P2P_GITHUB_TOKEN="ghp_YOUR_TOKEN_HERE"' >> ~/.bashrc
echo 'export P2P_GIST_ID="YOUR_GIST_ID_HERE"' >> ~/.bashrc

# Zsh 用户
echo 'export P2P_GITHUB_TOKEN="ghp_YOUR_TOKEN_HERE"' >> ~/.zshrc
echo 'export P2P_GIST_ID="xxx..."' >> ~/.zshrc

# 重新加载配置
source ~/.bashrc  # 或 source ~/.zshrc

# 方法 2: 临时设置（仅当前会话）
export P2P_GITHUB_TOKEN="ghp_xxx..."
export P2P_GIST_ID="1d3ee11b4bcdfd6ff16c888c6bcff3d6"
```

**Windows PowerShell:**

```powershell
# 临时设置
$env:P2P_GITHUB_TOKEN = "ghp_xxx..."
$env:P2P_GIST_ID = "1d3ee11b4bcdfd6ff16c888c6bcff3d6"

# 永久设置（用户级别）
[System.Environment]::SetEnvironmentVariable(
    "P2P_GITHUB_TOKEN", 
    "ghp_xxx...", 
    "User"
)
[System.Environment]::SetEnvironmentVariable(
    "P2P_GIST_ID", 
    "1d3ee11b4bcdfd6ff16c888c6bcff3d6", 
    "User"
)

# 验证
$env:P2P_GITHUB_TOKEN
$env:P2P_GIST_ID
```

**Docker 容器:**

```bash
docker run -it \
  -e P2P_GITHUB_TOKEN="ghp_xxx..." \
  -e P2P_GIST_ID="1d3ee11b4bcdfd6ff16c888c6bcff3d6" \
  p2p-app:latest
```

### 3.2 配置验证

**验证检查清单：**

```bash
#!/bin/bash
# verify_config.sh

echo "🔍 验证 GitHub Gist 配置..."

# 1. 检查环境变量
if [ -z "$P2P_GITHUB_TOKEN" ]; then
    echo "❌ P2P_GITHUB_TOKEN 未设置"
    exit 1
else
    echo "✅ Token 已设置 (${P2P_GITHUB_TOKEN:0:7}...)"
fi

if [ -z "$P2P_GIST_ID" ]; then
    echo "❌ P2P_GIST_ID 未设置"
    exit 1
else
    echo "✅ Gist ID: $P2P_GIST_ID"
fi

# 2. 验证 Token 有效性
echo ""
echo "🔐 验证 Token 权限..."
user_info=$(curl -s -H "Authorization: token $P2P_GITHUB_TOKEN" \
                    https://api.github.com/user)

if echo "$user_info" | grep -q '"login"'; then
    username=$(echo "$user_info" | jq -r '.login')
    echo "✅ Token 有效，用户: $username"
else
    echo "❌ Token 无效或已过期"
    echo "响应: $user_info"
    exit 1
fi

# 3. 验证 Gist 访问权限
echo ""
echo "📄 验证 Gist 访问..."
gist_info=$(curl -s -H "Authorization: token $P2P_GITHUB_TOKEN" \
                    "https://api.github.com/gists/$P2P_GIST_ID")

if echo "$gist_info" | grep -q '"id"'; then
    echo "✅ Gist 可访问"
    echo "   URL: https://gist.github.com/$username/$P2P_GIST_ID"
else
    echo "❌ Gist 不存在或无权限"
    echo "响应: $gist_info"
    exit 1
fi

# 4. 检查 Gist 内容
echo ""
echo "📋 检查 Gist 内容..."
content=$(echo "$gist_info" | jq -r '.files."p2p_signal.json".content')

if [ "$content" = "{}" ] || [ "$content" = "null" ]; then
    echo "✅ Gist 内容正常（空或初始状态）"
else
    echo "⚠️  Gist 已有内容: $content"
    echo "   建议清空后使用"
fi

# 5. 测试写入权限
echo ""
echo "✏️  测试写入权限..."
test_content='{"test":"'$(date +%s)'"}'
write_response=$(curl -s -X PATCH \
    -H "Authorization: token $P2P_GITHUB_TOKEN" \
    -H "Content-Type: application/json" \
    -d "{\"files\":{\"p2p_signal.json\":{\"content\":\"$test_content\"}}}" \
    "https://api.github.com/gists/$P2P_GIST_ID")

if echo "$write_response" | grep -q '"id"'; then
    echo "✅ 写入权限正常"
    
    # 恢复初始状态
    curl -s -X PATCH \
        -H "Authorization: token $P2P_GITHUB_TOKEN" \
        -H "Content-Type: application/json" \
        -d '{"files":{"p2p_signal.json":{"content":"{}"}}}' \
        "https://api.github.com/gists/$P2P_GIST_ID" > /dev/null
    echo "   已恢复初始状态"
else
    echo "❌ 写入失败，Token 可能没有 gist 权限"
    exit 1
fi

echo ""
echo "🎉 所有检查通过！配置正确。"
```

**使用验证脚本：**
```bash
chmod +x verify_config.sh
./verify_config.sh
```

---

## 4. 编译项目

### 4.1 获取源码

```bash
# 克隆仓库（替换为实际仓库地址）
git clone https://github.com/your-username/p2p.git
cd p2p

# 或使用本地已有代码
cd /path/to/p2p
```

### 4.2 CMake 编译（推荐）

```bash
# 创建构建目录
mkdir -p build_cmake
cd build_cmake

# 配置（启用 DTLS 支持可选）
cmake .. -DWITH_DTLS=OFF -DTHREADED=ON

# 编译（使用多核加速）
make -j$(nproc)

# 验证编译产物
ls -lh p2p_ping/p2p_ping
# 预期输出: -rwxr-xr-x ... p2p_ping
```

### 4.3 Makefile 编译

```bash
# 直接编译
make clean
make -j4

# 验证
ls -lh build/example
```

### 4.4 编译选项

| 选项 | 说明 | 推荐值 |
|------|------|-------|
| `WITH_DTLS` | 启用 DTLS 加密传输 | OFF（Gist 信令不需要） |
| `THREADED` | 多线程支持 | ON |
| `DEBUG` | 调试符号 | 测试时 ON |

---

## 5. 本地测试

### 5.1 单机双实例测试

**终端 1: 启动 Alice（订阅者）**

```bash
cd /path/to/p2p

# 设置环境变量（如果未持久化）
export P2P_GITHUB_TOKEN="ghp_xxx..."
export P2P_GIST_ID="1d3ee11b4bcdfd6ff16c888c6bcff3d6"

# 启动 Alice
./build_cmake/p2p_ping/p2p_ping \
    --name alice \
    --github "$P2P_GITHUB_TOKEN" \
    --gist "$P2P_GIST_ID" \
    2>&1 | tee alice_local.log

# 预期输出:
# Running in GIST mode...
# [SIGNAL_PUB] Initialized as SUBSCRIBER
# [SIGNAL_PUB] Channel: 1d3ee11b4bcdfd6ff16c888c6bcff3d6
# [SIGNAL_PUB] Polling Gist for offers...
```

**终端 2: 启动 Bob（发布者）**

```bash
# 3-5 秒后启动 Bob
cd /path/to/p2p

export P2P_GITHUB_TOKEN="ghp_xxx..."
export P2P_GIST_ID="1d3ee11b4bcdfd6ff16c888c6bcff3d6"

./build_cmake/p2p_ping/p2p_ping \
    --name bob \
    --github "$P2P_GITHUB_TOKEN" \
    --gist "$P2P_GIST_ID" \
    --to alice \
    2>&1 | tee bob_local.log

# 预期输出:
# Running in GIST mode...
# [SIGNAL_PUB] Initialized as PUBLISHER
# [SIGNAL_PUB] Target: alice
# [SIGNAL_PUB] Updating Gist with encrypted payload...
```

**观察连接建立（10-20 秒）：**

Alice 终端:
```
[SIGNAL_PUB] Received valid signal from 'bob'
[ICE] Received New Remote Candidate: 0 -> 10.2.100.136:56741
[ICE] Received New Remote Candidate: 0 -> 198.10.0.1:56741
[ICE] Nomination successful! Using path 10.2.100.136:51202
[STATE] IDLE (0) -> CONNECTED (3)
[DATA] Sent PING
[DATA] Received: P2P_PING_ALIVE
```

Bob 终端:
```
[SIGNAL_PUB] Received valid signal from 'bob'
[ICE] Nomination successful! Using path 10.2.100.136:59249
[STATE] IDLE (0) -> CONNECTED (3)
[DATA] Sent PING
[DATA] Received: P2P_PING_ALIVE
```

### 5.2 查看 Gist 内容

```bash
# 方法 1: 浏览器
open "https://gist.github.com/$(whoami)/$P2P_GIST_ID"

# 方法 2: API
curl -H "Authorization: token $P2P_GITHUB_TOKEN" \
     "https://api.github.com/gists/$P2P_GIST_ID" \
     | jq '.files."p2p_signal.json".content' -r

# 方法 3: 直接 GET
curl "https://gist.githubusercontent.com/username/$P2P_GIST_ID/raw/p2p_signal.json"

# 预期内容（Base64 编码的加密数据）:
# {"offer":"Y2FuZGlkYXRlc...","answer":"cGVlcl9pZDphb..."}
```

---

## 6. 跨网络部署

### 6.1 准备工作

**清单：**
- [ ] 两台不同网络的设备（家庭 + 办公室 / 4G + WiFi）
- [ ] 两台设备都能访问 GitHub（无防火墙阻挡）
- [ ] 已编译好 `p2p_ping` 二进制文件
- [ ] Token 和 Gist ID 已配置

**网络拓扑示例：**
```
设备 A (家庭网络)                    设备 B (办公室网络)
NAT: 192.168.1.1                    NAT: 10.0.0.1
内网: 192.168.1.100                 内网: 10.0.0.50
公网: 123.45.67.89                  公网: 98.76.54.32
       ↓                                   ↓
       └──────────→ GitHub Gist ←──────────┘
              (信令交换)
       
       直连建立后绕过 GitHub ↓
       
       123.45.67.89:xxxxx ←─────→ 98.76.54.32:yyyyy
              (UDP 打洞 P2P 连接)
```

### 6.2 设备 A 配置

```bash
# SSH 登录设备 A 或本地操作
cd /path/to/p2p

# 设置环境变量
export P2P_GITHUB_TOKEN="ghp_xxx..."
export P2P_GIST_ID="1d3ee11b4bcdfd6ff16c888c6bcff3d6"

# 清空 Gist（确保干净状态）
curl -s -X PATCH \
    -H "Authorization: token $P2P_GITHUB_TOKEN" \
    -d '{"files":{"p2p_signal.json":{"content":"{}"}}}' \
    "https://api.github.com/gists/$P2P_GIST_ID"

# 启动 Alice
./build_cmake/p2p_ping/p2p_ping \
    --name alice \
    --github "$P2P_GITHUB_TOKEN" \
    --gist "$P2P_GIST_ID"
```

### 6.3 设备 B 配置

```bash
# SSH 登录设备 B
cd /path/to/p2p

# 相同的 Token 和 Gist ID！
export P2P_GITHUB_TOKEN="ghp_xxx..."  # 同一个
export P2P_GIST_ID="1d3ee11b4bcdfd6ff16c888c6bcff3d6"  # 同一个

# 等待 5-10 秒确保 Alice 已启动
sleep 10

# 启动 Bob
./build_cmake/p2p_ping/p2p_ping \
    --name bob \
    --github "$P2P_GITHUB_TOKEN" \
    --gist "$P2P_GIST_ID" \
    --to alice
```

### 6.4 监控连接

**设备 A（Alice）日志关键点：**
```
t0:  Running in GIST mode...
t10: [SIGNAL_PUB] Received valid signal from 'bob'
t12: [ICE] Received New Remote Candidate: 0 -> 98.76.54.32:xxxxx
t15: [ICE] Nomination successful!
t16: [STATE] CONNECTED (3)
```

**设备 B（Bob）日志关键点：**
```
t0:  Running in GIST mode...
t1:  [SIGNAL_PUB] Updating Gist...
t12: [SIGNAL_PUB] Received valid signal from 'bob'
t15: [ICE] Nomination successful!
t16: [STATE] CONNECTED (3)
```

---

## 7. 生产环境部署

### 7.1 安全加固

**Token 管理：**
```bash
# 使用密钥管理工具
# macOS Keychain
security add-generic-password \
    -s "P2P_GITHUB_TOKEN" \
    -a "$(whoami)" \
    -w "ghp_xxx..."

# 读取
TOKEN=$(security find-generic-password \
    -s "P2P_GITHUB_TOKEN" \
    -a "$(whoami)" \
    -w)

# Linux (使用 pass)
pass insert p2p/github_token
pass show p2p/github_token
```

**Gist 轮换策略：**
```bash
# 定期轮换 Gist（防止泄露）
# 每周/每月创建新 Gist
./create_gist.sh "$TOKEN" > new_gist_id.txt
```

### 7.2 监控和日志

**日志记录：**
```bash
# 使用 systemd 管理（Linux）
cat > /etc/systemd/system/p2p-alice.service << EOF
[Unit]
Description=P2P Alice Node
After=network.target

[Service]
Type=simple
User=p2puser
Environment="P2P_GITHUB_TOKEN=ghp_xxx..."
Environment="P2P_GIST_ID=xxx..."
ExecStart=/usr/local/bin/p2p_ping --name alice --github ...
Restart=always
StandardOutput=append:/var/log/p2p/alice.log
StandardError=append:/var/log/p2p/alice.error.log

[Install]
WantedBy=multi-user.target
EOF

systemctl enable p2p-alice
systemctl start p2p-alice
```

**监控脚本：**
```bash
#!/bin/bash
# monitor_p2p.sh

while true; do
    # 检查进程
    if ! pgrep -f "p2p_ping.*alice" > /dev/null; then
        echo "[$(date)] Alice 进程已停止，重启中..."
        systemctl restart p2p-alice
    fi
    
    # 检查连接状态
    if grep -q "CONNECTED" /var/log/p2p/alice.log; then
        echo "[$(date)] Alice 已连接"
    else
        echo "[$(date)] Alice 等待连接..."
    fi
    
    sleep 60
done
```

### 7.3 备份与恢复

**备份配置：**
```bash
# 备份环境配置
cat > p2p_config_backup.sh << EOF
#!/bin/bash
export P2P_GITHUB_TOKEN="ghp_xxx..."
export P2P_GIST_ID="xxx..."
export P2P_AUTH_KEY="your_secret_key"
EOF

# 加密保存
gpg -c p2p_config_backup.sh
# 输出: p2p_config_backup.sh.gpg

# 恢复时
gpg -d p2p_config_backup.sh.gpg | bash
```

---

## 8. 故障排查

### 常见问题速查表

| 问题 | 可能原因 | 解决方案 |
|------|---------|---------|
| HTTP 401 | Token 无效/过期 | 重新生成 Token |
| HTTP 403 | 权限不足/速率限制 | 检查 scope/等待重置 |
| HTTP 404 | Gist ID 错误 | 验证 Gist 存在 |
| 一直轮询无响应 | 对方未启动 | 确认双方都在运行 |
| 解密失败 | auth_key 不一致 | 使用相同编译版本 |
| ICE 失败 | NAT 阻挡 | 检查 STUN 服务器可达 |

**详细排查流程见：[GIST_SIGNALING_MECHANISM.md](GIST_SIGNALING_MECHANISM.md) 第 9 节**

---

## 9. 自动化脚本

### 一键启动脚本

```bash
#!/bin/bash
# p2p_start.sh - 一键启动 P2P 节点

set -e

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${SCRIPT_DIR}/build_cmake/p2p_ping/p2p_ping"

# 检查二进制
if [ ! -x "$BIN" ]; then
    echo -e "${RED}错误: p2p_ping 不存在或不可执行${NC}"
    echo "请先编译: cd build_cmake && make"
    exit 1
fi

# 检查环境变量
if [ -z "$P2P_GITHUB_TOKEN" ] || [ -z "$P2P_GIST_ID" ]; then
    echo -e "${RED}错误: 环境变量未设置${NC}"
    echo "请设置:"
    echo "  export P2P_GITHUB_TOKEN=\"ghp_xxx...\""
    echo "  export P2P_GIST_ID=\"xxx...\""
    exit 1
fi

# 菜单
echo -e "${GREEN}P2P Gist 信令启动器${NC}"
echo "========================"
echo "1) Alice (订阅者)"
echo "2) Bob (发布者)"
echo "3) 清空 Gist"
echo "4) 查看 Gist 内容"
echo "q) 退出"
echo ""
read -p "选择操作 [1-4/q]: " choice

case $choice in
    1)
        echo -e "${GREEN}启动 Alice...${NC}"
        $BIN --name alice --github "$P2P_GITHUB_TOKEN" --gist "$P2P_GIST_ID"
        ;;
    2)
        read -p "目标名称 [alice]: " target
        target=${target:-alice}
        echo -e "${GREEN}启动 Bob (连接到 $target)...${NC}"
        $BIN --name bob --github "$P2P_GITHUB_TOKEN" --gist "$P2P_GIST_ID" --to "$target"
        ;;
    3)
        echo -e "${YELLOW}清空 Gist...${NC}"
        curl -s -X PATCH \
            -H "Authorization: token $P2P_GITHUB_TOKEN" \
            -d '{"files":{"p2p_signal.json":{"content":"{}"}}}' \
            "https://api.github.com/gists/$P2P_GIST_ID" > /dev/null
        echo -e "${GREEN}完成${NC}"
        ;;
    4)
        echo -e "${YELLOW}Gist 内容:${NC}"
        curl -s -H "Authorization: token $P2P_GITHUB_TOKEN" \
            "https://api.github.com/gists/$P2P_GIST_ID" \
            | jq '.files."p2p_signal.json".content' -r | jq .
        ;;
    q)
        exit 0
        ;;
    *)
        echo -e "${RED}无效选择${NC}"
        exit 1
        ;;
esac
```

**使用方法：**
```bash
chmod +x p2p_start.sh
./p2p_start.sh
```

---

## 10. 总结

### 部署检查清单

- [ ] GitHub Token 已创建并验证
- [ ] Gist 已创建（Secret 类型）
- [ ] 环境变量已配置
- [ ] 项目已编译成功
- [ ] 本地测试通过
- [ ] 跨网络测试通过（如需要）
- [ ] 监控和日志已配置（生产环境）

### 下一步

✅ 基础部署完成后，建议：
1. 阅读 [GIST_SIGNALING_MECHANISM.md](GIST_SIGNALING_MECHANISM.md) 理解原理
2. 查看 [TESTING.md](../TESTING.md) 了解测试用例和性能指标
3. 参考 [SECURITY.md](SECURITY.md) 加强安全防护

---

**技术支持：**
- GitHub Issues: [项目地址]/issues
- 文档首页: [README.md](README.md)
