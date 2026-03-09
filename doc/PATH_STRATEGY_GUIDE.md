# 多路径管理与策略选择指南

## 概述

从当前版本开始，P2P 库支持**多路径并行传输**架构，可以同时维护多条传输路径（LAN/PUNCH/RELAY/TURN），并根据配置的策略自动选择最优路径。

这一架构解决了以下问题：
- ✅ 中继不再是终态，可自动恢复到直连
- ✅ 路径动态切换，适应网络变化
- ✅ 支持不同使用场景的策略选择

## 架构变更

### 旧架构（单路径状态机）

```
P2P_STATE_CONNECTED → P2P_STATE_RELAY（无法恢复）
```

**问题**：
- 状态切换是单向的：CONNECTED → RELAY 后无法回退
- NAT 连接恢复后，仍停留在中继模式
- 浪费带宽，增加延迟

### 新架构（多路径并行）

```
Path Manager
├── LAN Path:    ACTIVE  (RTT: 2ms,  Loss: 0%)   [优先级最高]
├── PUNCH Path:  ACTIVE  (RTT: 50ms, Loss: 1%)   [直连路径]
├── RELAY Path:  DEGRADED (RTT: 150ms, Loss: 5%) [备用路径]
└── TURN Path:   FAILED  -                       [已失效]

当前活跃路径: LAN (自动选择)
```

**优势**：
- 所有路径并行探测，保持活跃
- NAT 恢复时自动切换回直连
- 根据策略优化成本或性能

## 路径选择策略

### 1. CONNECTION_FIRST（直连优先）🎯 默认

**适用场景**：
- 企业环境（带宽成本敏感）
- IoT 设备（流量计费）
- 希望最小化中继使用

**选择规则**：
```
优先级: LAN > PUNCH > RELAY > TURN
```

**配置示例**：
```c
p2p_config_t cfg = {0};
cfg.path_strategy = 0; // CONNECTION_FIRST
// 或者不设置（默认就是 0）
```

**实际效果**：
- 只要直连可用，永远不使用中继
- NAT 超时后临时降级到中继
- NAT 恢复后立即切回直连
- 最小化云服务成本

### 2. PERFORMANCE_FIRST（性能优先）⚡

**适用场景**：
- 实时音视频通话
- 在线游戏
- 对延迟极度敏感的应用

**选择规则**：
```
综合评分 = 0.5 × RTT分数 + 0.3 × 丢包分数 + 0.2 × 抖动分数
选择得分最高的路径
```

**配置示例**：
```c
p2p_config_t cfg = {0};
cfg.path_strategy = 1; // PERFORMANCE_FIRST
```

**实际效果**：
- 优先选择延迟最低的路径
- 如果中继比直连更快，会使用中继
- 例如：某些极端 NAT 环境下，通过优质中继比直连更快
- 适合音视频场景

### 3. HYBRID（混合模式）🔀

**适用场景**：
- 通用场景
- 平衡成本和性能
- 类似 WebRTC 默认行为

**选择规则**：
```
1. 优先使用直连（LAN/PUNCH）
2. 如果直连性能严重退化（RTT > 100ms 或 丢包 > 1%），
   且中继性能显著更好（RTT 差距 > 50ms），则切换到中继
3. 否则继续使用直连（节省成本）
```

**配置示例**：
```c
p2p_config_t cfg = {0};
cfg.path_strategy = 2; // HYBRID
```

**实际效果**：
- 大部分情况使用直连
- 网络退化时自动降级中继
- 恢复后自动切回直连
- 平衡用户体验和成本

## 完整配置示例

### 场景 1：企业内网通信（最小化成本）

```c
#include <p2p.h>

p2p_config_t cfg = {0};
cfg.bind_port = 0;
cfg.signaling_mode = P2P_SIGNALING_MODE_COMPACT;
cfg.server_host = "signal.example.com";
cfg.server_port = 5678;
cfg.use_ice = true;
cfg.stun_server = "stun.l.google.com";
cfg.stun_port = 19302;

// 直连优先策略（默认）
cfg.path_strategy = 0; // CONNECTION_FIRST

p2p_handle_t h = p2p_create("client1", &cfg);
```

### 场景 2：在线游戏（极致性能）

```c
p2p_config_t cfg = {0};
cfg.bind_port = 0;
cfg.signaling_mode = P2P_SIGNALING_MODE_RELAY;
cfg.server_host = "game-signal.example.com";
cfg.server_port = 8080;
cfg.use_ice = true;
cfg.stun_server = "stun.game.com";
cfg.stun_port = 3478;

// 性能优先策略
cfg.path_strategy = 1; // PERFORMANCE_FIRST

p2p_handle_t h = p2p_create("player123", &cfg);
```

### 场景 3：视频会议（平衡模式）

```c
p2p_config_t cfg = {0};
cfg.bind_port = 0;
cfg.signaling_mode = P2P_SIGNALING_MODE_PUBSUB;
cfg.gh_token = "ghp_xxx";
cfg.gist_id = "abc123";
cfg.use_ice = true;
cfg.stun_server = "stun.l.google.com";
cfg.stun_port = 19302;
cfg.dtls_backend = 1; // DTLS 加密 (1=mbedtls, 2=openssl)

// 混合策略
cfg.path_strategy = 2; // HYBRID

p2p_handle_t h = p2p_create("user@example.com", &cfg);
```

## 路径切换行为

### 自动切换触发条件

1. **RTT 改善** >= 50ms → 切换
2. **丢包率** > 5% → 切换到更好的路径
3. **路径失效** → 自动故障转移

### 切换冷却

为避免频繁抖动，路径切换有 **5 秒冷却期**：

```
时间线:
  0s: 检测到 PUNCH 路径更优，切换
  2s: RELAY 路径短暂恢复，但仍在冷却期 → 不切换
  6s: 冷却期结束，如果 RELAY 确实更优 → 切换
```

## 监控与调试

### 获取当前路径

```c
int current_path = p2p_path(h);
switch (current_path) {
    case P2P_PATH_LAN:   printf("LAN 直连\n"); break;
    case P2P_PATH_PUNCH: printf("NAT 打洞\n"); break;
    case P2P_PATH_RELAY: printf("服务器中继\n"); break;
}
```

### 查看路径统计

路径管理器会自动输出关键事件日志：

```
[INFO] Path manager initialized with strategy: 0 (0=conn,1=perf,2=hybrid)
[INFO] Added PUNCH path to path manager, idx=0
[INFO] Selected path: PUNCH (idx=0)
[INFO] Path switched: RELAY -> LAN (RTT: 150 -> 2 ms)
```

## 高级调优

### 修改切换阈值（未来版本）

```c
// 当前阈值是内置的，未来可能开放配置：
// cfg.path_switch_rtt_threshold_ms = 30;  // RTT 改善 30ms 即切换
// cfg.path_switch_loss_threshold = 0.03;  // 丢包率 3% 触发切换
// cfg.path_switch_cooldown_ms = 3000;     // 冷却时间 3 秒
```

### 禁用某些路径

```c
// 禁用 LAN 路径升级（仅用于测试）
cfg.disable_lan_shortcut = true;
```

## 与旧代码兼容性

**完全向后兼容**！

不设置 `path_strategy` 时：
- 默认使用 `CONNECTION_FIRST`（直连优先）
- 行为与旧版本类似，但增加了自动恢复能力

旧代码无需修改即可享受新架构带来的：
- ✅ 中继 → 直连自动恢复
- ✅ 更稳定的连接
- ✅ 更少的带宽浪费

## 最佳实践

### 1. 选择合适的策略

| 应用类型         | 推荐策略           | 原因                    |
|------------------|--------------------|-----------------------|
| 文件传输         | CONNECTION_FIRST   | 节省带宽成本            |
| 实时音视频       | PERFORMANCE_FIRST  | 最小化延迟              |
| 即时通讯         | HYBRID             | 平衡体验和成本          |
| IoT 设备         | CONNECTION_FIRST   | 流量计费敏感            |
| 在线游戏         | PERFORMANCE_FIRST  | 对延迟极度敏感          |

### 2. 配置 STUN/TURN

即使使用中继，也应配置 STUN/TURN：
- STUN 用于检测公网地址
- TURN 作为最终备份（未来支持）

### 3. 监控日志

新架构会输出详细日志，建议：
- 开发阶段：设置 `P2P_LOG_LEVEL_DEBUG`
- 生产环境：设置 `P2P_LOG_LEVEL_INFO`

## 常见问题

### Q1: 为什么仍然优先使用直连？

即使在 `PERFORMANCE_FIRST` 模式下，如果直连性能足够好（RTT < 100ms，丢包 < 1%），仍会优先使用直连。这是因为中继会增加服务器负载和成本。

### Q2: 切换会导致数据丢失吗？

不会。路径切换是透明的：
- 切换时只改变底层传输地址
- 上层可靠传输层（Reliable ARQ）保证无丢包
- 应用层无感知

### Q3: 如何强制使用中继？

新架构不支持强制路径。如果必须使用中继：
1. 禁用 STUN（不收集公网候选）
2. 配置 TURN 服务器
3. NAT 打洞失败后自动降级到中继

### Q4: 策略可以动态切换吗？

当前版本不支持运行时切换策略。需要在创建会话时指定。未来版本可能支持。

## 技术细节

### 路径状态机

```
PATH_STATE_INIT → PATH_STATE_PROBING → PATH_STATE_ACTIVE
                                         ↓
                PATH_STATE_FAILED ← PATH_STATE_DEGRADED
                        ↓
                PATH_STATE_RECOVERING → PATH_STATE_ACTIVE
```

### Phase 2: RTT 实时测量与丢包统计

**自动测量机制**：
- 每次收到 ACK，自动更新路径 RTT
- 使用 EWMA（指数加权移动平均）平滑 RTT 波动
- 实时计算丢包率（滑动窗口）

**RTT 样本缓冲**：
- 保留最近 10 个 RTT 样本
- 用于计算 RTT 方差（抖动）
- 支持趋势分析

**统计指标**：
```c
path_info_t {
    uint32_t rtt_ms;          // 当前 RTT
    uint32_t rtt_min;         // 最小 RTT（基线）
    uint32_t rtt_max;         // 最大 RTT
    uint32_t rtt_variance;    // 抖动
    float    loss_rate;       // 丢包率
}
```

### Phase 3: 路径质量预测

**质量等级评估**：
| 等级 | RTT | 丢包率 | 说明 |
|------|-----|--------|------|
| EXCELLENT | < 50ms | < 1% | 优秀（适合实时音视频） |
| GOOD | < 100ms | < 3% | 良好（适合游戏） |
| FAIR | < 200ms | < 5% | 一般（可用） |
| POOR | < 500ms | < 10% | 较差（勉强可用） |
| BAD | ≥ 500ms | ≥ 10% | 很差（建议切换） |

**质量评分算法**：
```
quality_score = 0.5 × RTT分数 + 0.3 × 丢包分数 + 0.2 × 抖动分数
```

**趋势预测**：
- 基于最近 10 个 RTT 样本
- 比较前半部分和后半部分平均值
- 输出趋势值：
  - **-1.0 到 0.0**：质量改善（RTT 下降）
  - **0.0**：质量稳定
  - **0.0 到 1.0**：质量恶化（RTT 上升）

**稳定性评分**：
- 基于 RTT 变异系数（CV = 标准差 / 平均值）
- 0-100 分制（100 = 完全稳定）
- 用于识别网络抖动

**质量 API**：
```c
// 获取路径质量等级
path_quality_t quality = path_manager_get_quality(&pm, path_chn);

// 获取质量评分（0.0-1.0）
float score = path_manager_get_quality_score(&pm, path_chn);

// 预测质量趋势（-1.0 到 1.0）
float trend = path_manager_get_quality_trend(&pm, path_chn);

// 获取稳定性（0-100）
int stability = pm.paths[path_chn].stability_score;
```

### 路径切换决策

**触发条件（增强版）**：
1. **RTT 改善** >= 50ms
2. **丢包率** > 5%
3. **路径失效** 连续超时 3 次
4. **质量预测** 趋势持续恶化

**切换策略矩阵**：

| 策略 | LAN | PUNCH | RELAY | 依据 |
|------|-----|-------|-------|------|
| CONNECTION_FIRST | 优先 | 次优 | 备用 | 成本 |
| PERFORMANCE_FIRST | 取决于 RTT | 取决于 RTT | 取决于 RTT | 性能 |
| HYBRID | 优先（质量好） | 优先（质量好） | 降级（性能差） | 平衡 |

### 性能指标收集

**实时更新**：
**实时更新**：
- 每次收到 ACK：更新 RTT
- 每次数据接收：更新活跃时间
- 每 500ms：健康检查
- 每 5s：路径质量重评估

**健康检查**：
- 活跃路径：每 500ms 检查一次
- 失效路径：每 30s 探测恢复
- 降级路径：监控是否恢复正常

## 高级用法

### 查询路径质量

```c
// 检查当前路径质量
if (s->path_mgr.active_path >= 0) {
    path_quality_t quality = path_manager_get_quality(&s->path_mgr, 
                                                      s->path_mgr.active_path);
    
    switch (quality) {
        case PATH_QUALITY_EXCELLENT:
            printf("路径优秀\n");
            break;
        case PATH_QUALITY_GOOD:
            printf("路径良好\n");
            break;
        case PATH_QUALITY_POOR:
            printf("路径较差，考虑切换\n");
            break;
        case PATH_QUALITY_BAD:
            printf("路径很差，立即切换\n");
            break;
    }
}
```

### 预测网络趋势

```c
// 预测路径质量趋势
float trend = path_manager_get_quality_trend(&s->path_mgr, s->path_mgr.active_path);

if (trend < -0.3) {
    printf("网络质量正在改善\n");
} else if (trend > 0.3) {
    printf("网络质量正在恶化，准备切换\n");
} else {
    printf("网络质量稳定\n");
}
```

### 获取详细统计

```c
// 获取路径详细信息
path_info_t *path = path_manager_get_active_path(&s->path_mgr);
if (path) {
    printf("路径: %s\n", p2p_path_type_str(path->type));
    printf("RTT: %u ms (min: %u, max: %u)\n", 
           path->rtt_ms, path->rtt_min, path->rtt_max);
    printf("丢包率: %.2f%%\n", path->loss_rate * 100);
    printf("抖动: %u ms\n", path->rtt_variance);
    printf("质量: %s (评分: %.2f)\n", 
           path_quality_str(path->quality), path->quality_score);
    printf("稳定性: %d/100\n", path->stability_score);
}
```

## Phase 4: 动态阈值与防抖动 🎯 已完成

### 功能概述

为解决路径频繁切换问题，Phase 4 引入了智能防抖动机制和动态阈值配置。

### 1. 动态阈值配置

每种路径类型可配置独立的切换阈值：

```c
// 为 LAN 路径设置严格阈值
path_manager_set_threshold(&pm, P2P_PATH_LAN,
    20,      // RTT 阈值 20ms
    0.01f,   // 丢包率 1%
    3000,    // 冷却时间 3s
    1000);   // 稳定窗口 1s

// 为 RELAY 路径设置宽松阈值
path_manager_set_threshold(&pm, P2P_PATH_RELAY,
    100,     // RTT 阈值 100ms
    0.10f,   // 丢包率 10%
    5000,    // 冷却时间 5s
    2000);   // 稳定窗口 2s
```

### 2. 路径切换历史

系统自动记录最近 20 次路径切换历史，用于分析和调试：

```c
path_switch_record_t history[20];
int count = path_manager_get_switch_history(&pm, history, 20);

for (int i = 0; i < count; i++) {
    printf("切换 #%d: %s(%dms, %.1f%%) → %s(%dms, %.1f%%) | %s\n",
        i + 1,
        p2p_path_type_str(history[i].from_type),
        history[i].from_rtt_ms,
        history[i].from_loss_rate * 100,
        p2p_path_type_str(history[i].to_type),
        history[i].to_rtt_ms,
        history[i].to_loss_rate * 100,
        history[i].reason);
}

// 示例输出：
// 切换 #1: RELAY(150ms, 5.0%) → PUNCH(50ms, 1.0%) | 质量改善
// 切换 #2: PUNCH(80ms, 8.0%) → RELAY(120ms, 2.0%) | 丢包率高
```

### 3. 防抖动算法

**问题**：网络抖动可能导致路径频繁切换（"ping-pong"），影响稳定性。

**解决方案**：
- **冷却时间**：切换后 N 秒内禁止再次切换
- **稳定窗口**：目标路径需持续 N 秒表现良好才切换
- **频率检测**：30 秒内超过 5 次切换 → 触发防抖

```c
// 尝试切换路径（自动防抖）
int result = path_manager_switch_path(&pm, target_path, "Performance优化", now_ms);

switch (result) {
    case 0:
        printf("✅ 成功切换到路径 %d\n", target_path);
        break;
    case 1:
        printf("⏳ 防抖延迟，稍后重试\n");
        break;
    case -1:
        printf("❌ 切换失败\n");
        break;
}
```

### 4. 切换频率分析

```c
// 分析最近 30 秒的切换频率
int switches = path_manager_get_switch_frequency(&pm, 30000);

if (switches > 5) {
    printf("⚠️  路径抖动检测：30秒内切换 %d 次\n", switches);
    printf("建议：调整阈值或延长冷却时间\n");
}
```

### 技术细节

| 特性 | 描述 | 默认值 |
|------|------|--------|
| 历史记录 | 环形缓冲区，最多 20 条 | 20 |
| 冷却时间 | 切换后的静默期 | 5 秒 |
| 稳定窗口 | 目标路径需稳定的时长 | 2 秒 |
| 抖动阈值 | 30 秒内触发防抖的切换次数 | 5 次 |

---

## Phase 5: TURN 路径支持 🎯 已完成

### 功能概述

TURN（Traversal Using Relays around NAT）作为最终备份方案，当所有其他路径失效时启用。

### 1. TURN 配置

```c
// 配置 TURN 支持
path_manager_configure_turn(&pm,
    true,   // 启用 TURN
    5,      // 成本倍数（相对于 RELAY）
    0,      // 带宽限制（0=无限制）
    true);  // 仅作为最终备份
```

**成本模型**：
- LAN: 0（免费）
- PUNCH: 0（免费）
- RELAY: 5（中等成本）
- TURN: 10（高成本，默认配置下为 RELAY 的 2 倍）

### 2. 添加 TURN 路径

```c
// 从 TURN 服务器分配中继地址后
struct sockaddr_in turn_addr;
inet_pton(AF_INET, turn_server_ip, &turn_addr.sin_addr);
turn_addr.sin_port = htons(turn_server_port);

int turn_idx = path_manager_add_turn_path(&pm, &turn_addr);
if (turn_idx >= 0) {
    printf("🔄 TURN 路径已添加: %d\n", turn_idx);
}
```

### 3. 自动激活逻辑

TURN 路径的激活遵循以下规则：

```
if (use_as_last_resort == true) {
    // 仅当所有其他路径（LAN/PUNCH/RELAY）都失效时启用
    if (all_other_paths_failed) {
        activate_turn();
    }
} else {
    // 作为普通路径参与选择（根据性能）
    participate_in_selection();
}
```

**路径选择流程（CONNECTION_FIRST 策略）**：

1. 尝试 LAN 路径 ✅
2. 尝试 PUNCH 路径 ✅
3. 尝试 RELAY 路径 ✅
4. ❌ 所有直连和中继失效
5. 👉 激活 TURN 路径（最终备份）

### 4. TURN 统计

```c
uint64_t sent, recv;
uint32_t rtt;

if (path_manager_get_turn_stats(&pm, &sent, &recv, &rtt) == 0) {
    printf("📊 TURN 统计:\n");
    printf("   发送: %.2f MB\n", sent / 1024.0 / 1024.0);
    printf("   接收: %.2f MB\n", recv / 1024.0 / 1024.0);
    printf("   延迟: %u ms\n", rtt);
    
    // 成本估算（假设 TURN 流量 $0.05/GB）
    float cost = (sent + recv) / 1024.0 / 1024.0 / 1024.0 * 0.05;
    printf("   估计成本: $%.4f\n", cost);
}
```

### 5. 路径优先级对比

| 策略 | LAN | PUNCH | RELAY | TURN |
|------|-----|-------|-------|------|
| CONNECTION_FIRST | 1️⃣ | 2️⃣ | 3️⃣ | 4️⃣（仅备份） |
| PERFORMANCE_FIRST | 按 RTT 评分 | 按 RTT 评分 | 评分×0.8 | 评分×0.8 |
| HYBRID | 优先 | 优先 | 性能差时使用 | 无其他路径时使用 |

### 技术细节

| 配置项 | 描述 | 默认值 |
|--------|------|--------|
| 成本倍数 | 相对于 RELAY 的成本权重 | 5（即 TURN 成本 = 2.5×RELAY） |
| 带宽限制 | TURN 最大带宽（bps） | 0（无限制） |
| 仅备份模式 | 仅在其他路径失效时启用 | true |

### 实际应用场景

**场景 1：企业防火墙（严格出站规则）**

```
LAN:   ❌ 不同子网
PUNCH: ❌ 防火墙阻止 UDP 打洞
RELAY: ❌ 中继服务器也被封锁
TURN:  ✅ 启用（唯一可用路径）
```

**场景 2：对称 NAT + 运营商 NAT**

```
LAN:   ❌ 不同网络
PUNCH: ❌ 对称 NAT 无法打洞
RELAY: ✅ 可用（优先使用）
TURN:  🟡 备用（RELAY 失效时启用）
```

---

## 未来规划

- [x] ~~Phase 2: RTT 实时测量与丢包统计~~ **已完成**
- [x] ~~Phase 3: 路径质量预测~~ **已完成**
- [x] ~~Phase 4: 动态阈值与防抖动~~ **已完成**
- [x] ~~Phase 5: TURN 路径支持~~ **已完成**
- [ ] 运行时策略切换
- [ ] 自定义策略回调
- [ ] 路径统计导出（Prometheus 指标）
- [ ] 自适应阈值调整（基于历史数据）
- [ ] 带宽估算与流量控制

## 参考资料

- [P2P_PROTOCOL.md](P2P_PROTOCOL.md) - 协议详细说明
- [ARCHITECTURE.md](ARCHITECTURE.md) - 架构概览
- [CONNECT_API.md](../CONNECT_API.md) - API 文档

---

**版本**: 0.3.0  
**更新日期**: 2026-03-01  
**新增功能**: Phase 4 (动态阈值防抖动) + Phase 5 (TURN 支持)

