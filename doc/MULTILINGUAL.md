# 多语言支持说明

## 概述

P2P 库现在支持多语言日志输出，包括：
- **英文（默认）**：语言代码 `0` (P2P_LANG_EN)
- **中文**：语言代码 `1` (P2P_LANG_ZH)，需编译时启用

## 编译选项

### CMake 编译

#### 默认编译（仅英文）
```bash
cd build_cmake
cmake ..
make
```

#### 启用中文支持
```bash
cd build_cmake
cmake -DENABLE_CHINESE=ON ..
make
```

### 直接使用 gcc/clang

#### 默认编译（仅英文）
```bash
gcc my_app.c src/p2p_lang.c -I.
```

#### 启用中文支持
```bash
gcc -DP2P_ENABLE_CHINESE my_app.c src/p2p_lang.c -I.
```

## API 使用

### 在配置中指定语言

```c
#include "p2p.h"

p2p_config_t cfg = {0};
cfg.local_peer_id = "alice";
cfg.language = 0;  // 0=English (默认), 1=中文
// ... 其他配置

p2p_session_t *session = p2p_create(&cfg);
```

### 运行时切换语言

```c
#include "p2p_lang.h"

// 切换到中文（仅在编译时启用中文支持时有效）
p2p_set_language(P2P_LANG_ZH);

// 切换到英文
p2p_set_language(P2P_LANG_EN);

// 获取当前语言
p2p_language_t lang = p2p_get_language();
```

### 使用消息 ID

```c
#include "p2p_lang.h"

// 使用 MSG() 宏获取当前语言的消息
printf("[NAT] %s: %s\n", 
       MSG(MSG_NAT_DETECTION_COMPLETED), 
       MSG(MSG_NAT_PORT_RESTRICTED));
```

## 输出示例

### 英文模式
```
[NAT] Detection completed: Port Restricted Cone NAT
[ICE] Nomination successful! Using Host (Local Network) path 192.168.1.100:5000 - Direct LAN connection
[RELAY] Received ACK (status=0)
[STATE] Connected
```

### 中文模式
```
[NAT] 检测完成: 端口受限锥形 NAT
[ICE] 协商成功！使用 本地网络 路径 192.168.1.100:5000 - 局域网直连
[RELAY] 收到 ACK (status=0)
[STATE] 已连接
```

## 二进制体积影响

- **仅英文**：中文词表不会被编译进二进制文件，节省约 2-3 KB
- **启用中文**：包含完整的中英文词表

## 测试

运行测试脚本验证编译选项：
```bash
cd /Users/wenpeng/dev/c/p2p
./test/test_lang_compile.sh
```

## 注意事项

1. **默认行为**：如果编译时未启用中文支持，即使设置 `language=1`，也会强制使用英文
2. **配置优先级**：`p2p_create()` 会根据 `cfg->language` 自动设置语言
3. **运行时切换**：可以在运行时调用 `p2p_set_language()` 切换语言
