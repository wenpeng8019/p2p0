/*
 * Auto-generated language strings
 */

#include "LANG.h"

/* Embedded cn language table */
static const char* s_lang_cn[LA_NUM] = {
    [LA_W1] = "已禁用",  /* SID:1 */
    [LA_W2] = "已启用",  /* SID:2 */
    [LA_S3] = "[TCP] 用户列表已截断（用户过多）\n",  /* SID:3 */
    [LA_S4] = "收到关闭信号，正在优雅退出...",  /* SID:4 */
    [LA_S5] = "正在关闭...\n",  /* SID:5 */
    [LA_F6] = "%s 来自 '%.*s'：新实例(旧=%u 新=%u)，重置会话\n",  /* SID:6 */
    [LA_F7] = "%s：已接受，本地='%.*s'，远端='%.*s'，inst_id=%u，cands=%d\n",  /* SID:7 */
    [LA_F8] = "%s：已接受，释放 '%s' -> '%s' 的槽位\n",  /* SID:8 */
    [LA_F9] = "%s：无效载荷(len=%zu)\n",  /* SID:9 */
    [LA_F10] = "%s：数据过大(len=%d)\n",  /* SID:10 */
    [LA_F11] = "%s：来自 %s 的 instance_id=0 无效\n",  /* SID:11 */
    [LA_F12] = "%s：来自客户端的中继标志无效\n",  /* SID:12 */
    [LA_F13] = "%s：无效 seq=%u\n",  /* SID:13 */
    [LA_F14] = "%s：未找到匹配的待处理消息(sid=%u)\n",  /* SID:14 */
    [LA_F15] = "%s：未找到匹配的待处理消息(sid=%u，期望=%u)\n",  /* SID:15 */
    [LA_F16] = "%s：过期 sid=%u(当前=%u)，已忽略\n",  /* SID:16 */
    [LA_F17] = "%s：空闲状态下过期 sid=%u(上次=%u)，已忽略\n",  /* SID:17 */
    [LA_F18] = "%s：节点 '%s' 不在线，拒绝 sid=%u\n",  /* SID:18 */
    [LA_F19] = "%s：等待节点 '%.*s' 注册\n",  /* SID:19 */
    [LA_F20] = "% 再见！\n",  /* SID:20 */
    [LA_F21] = "无效端口号 %d（范围：1-65535）\n",  /* SID:21 */
    [LA_F22] = "无效探测端口 %d（范围：0-65535）\n",  /* SID:22 */
    [LA_F23] = "% NAT 探测已禁用（绑定失败）\n",  /* SID:23 */
    [LA_F24] = "NAT 探测套接字在端口 %d 上监听\n",  /* SID:24 */
    [LA_F25] = "NAT 探测：%s（端口 %d）\n",  /* SID:25 */
    [LA_F26] = "P2P 信令服务器在端口 %d 上监听（TCP + UDP）...\n",  /* SID:26 */
    [LA_F27] = "PEER_INFO 重传失败：%s <-> %s（%d 次尝试后放弃）\n",  /* SID:27 */
    [LA_F28] = "配对完成：'%.*s'(%d 候选) <-> '%.*s'(%d 候选)\n",  /* SID:28 */
    [LA_F29] = "中继支持：%s\n",  /* SID:29 */
    [LA_F30] = "发送 %s：映射地址=%s:%d\n",  /* SID:30 */
    [LA_F31] = "发送 %s：状态=错误（无可用槽位）\n",  /* SID:31 */
    [LA_F32] = "在端口 %d 上启动 P2P 信令服务器\n",  /* SID:32 */
    [LA_F33] = "超时并清理配对 '%s' -> '%s'（不活跃 %.1f 秒）\n",  /* SID:33 */
    [LA_F34] = "来自 %s 的未知数据包类型 0x%02x\n",  /* SID:34 */
    [LA_F35] = "[中继] %s seq=0 来自客户端 %s（仅服务端使用，已丢弃）\n",  /* SID:35 */
    [LA_F36] = "[中继] %s：无效载荷(len=%zu)\n",  /* SID:36 */
    [LA_F37] = "[TCP]   → 转发来自 '%s' 的 OFFER（%d 候选，%d 字节）\n",  /* SID:37 */
    [LA_F38] = "[TCP]   → 发送来自 '%s' 的空 OFFER（存储已满，反向连接）\n",  /* SID:38 */
    [LA_F39] = "[TCP] 所有待处理候选已刷新到 '%s'\n",  /* SID:39 */
    [LA_F40] = "[TCP] 为离线用户 '%s' 缓存了 %d 个候选（总计=%d/%d）\n",  /* SID:40 */
    [LA_F41] = "[TCP] 为离线用户 '%s' 缓存了 %d 个候选，存储已满（%d/%d）\n",  /* SID:41 */
    [LA_F42] = "[TCP] 无法为离线用户 '%s' 分配槽位\n",  /* SID:42 */
    [LA_F43] = "[TCP] 错误：来自节点 '%s' 的魔数无效\n",  /* SID:43 */
    [LA_F44] = "[TCP] 从 %s 接收载荷失败\n",  /* SID:44 */
    [LA_F45] = "[TCP] 从 %s 接收目标名称失败\n",  /* SID:45 */
    [LA_F46] = "[TCP] 向 %s 发送 CONNECT_ACK 失败(sent_hdr=%d, sent_payload=%d)\n",  /* SID:46 */
    [LA_F47] = "[TCP] 正在将 %d 个待处理候选从 '%s' 刷新到 '%s'...\n",  /* SID:47 */
    [LA_F48] = "[TCP] 信息：节点 '%s' 已登录\n",  /* SID:48 */
    [LA_F49] = "% [TCP] 已达最大节点数，拒绝连接\n",  /* SID:49 */
    [LA_F50] = "[TCP] 将离线槽位中 %d 个待处理候选（发送方='%s'）合并到 '%s' 的在线槽位\n",  /* SID:50 */
    [LA_F51] = "[TCP] 来自 %s:%d 的新连接\n",  /* SID:51 */
    [LA_F52] = "[TCP] 新发送方 '%s' 替换旧发送方 '%s'（丢弃 %d 个旧候选）\n",  /* SID:52 */
    [LA_F53] = "[TCP] 来自 %s 的载荷过大（%u 字节）\n",  /* SID:53 */
    [LA_F54] = "[TCP] 从 %s 向 %s 中继 %s（%u 字节）\n",  /* SID:54 */
    [LA_F55] = "[TCP] 向 '%s' 发送了 %s，包含 %d 个候选（来自 '%s'）\n",  /* SID:55 */
    [LA_F56] = "[TCP] 向 %s 发送了 CONNECT_ACK(status=%d, candidates_acked=%d)\n",  /* SID:56 */
    [LA_F57] = "[TCP] '%s' 的存储已满（已缓存=%d，已丢弃=%d）\n",  /* SID:57 */
    [LA_F58] = "[TCP] 存储满指示已刷新到 '%s'\n",  /* SID:58 */
    [LA_F59] = "[TCP] 存储已满，已记录 '%s' 到 '%s' 的连接意向\n",  /* SID:59 */
    [LA_F60] = "[TCP] 存储已满，正在刷新 '%s' 到 '%s' 的连接意向（发送空 OFFER）...\n",  /* SID:60 */
    [LA_F61] = "[TCP] 目标 %s 离线，正在缓存候选...\n",  /* SID:61 */
    [LA_F62] = "[TCP] 来自 %s 的未知消息类型 %d\n",  /* SID:62 */
    [LA_F63] = "[TCP] 详细：已向 '%s' 发送 %s\n",  /* SID:63 */
    [LA_F64] = "[TCP] 详细：节点 '%s' 已断开\n",  /* SID:64 */
    [LA_F65] = "[TCP] 警告：客户端 '%s' 超时（不活跃 %.1f 秒）\n",  /* SID:65 */
    [LA_F66] = "[UDP] %s 从 %s 接收，seq=%u，flags=0x%02x，len=%zu\n",  /* SID:66 */
    [LA_F67] = "[UDP] %s 向 %s 发送失败(%d)\n",  /* SID:67 */
    [LA_F68] = "[UDP] %s 向 %s 发送，seq=%u，flags=0x00，len=%d\n",  /* SID:68 */
    [LA_F69] = "[UDP] %s 向 %s 发送，seq=0，flags=0，len=%d\n",  /* SID:69 */
    [LA_F70] = "[UDP] %s 向 %s 发送，seq=0，flags=0x%02x，len=%d\n",  /* SID:70 */
    [LA_F71] = "[UDP] %s 向 %s:%d 发送失败(%d)\n",  /* SID:71 */
    [LA_F72] = "[UDP] %s 向 %s:%d 发送，seq=0，flags=0，len=%d\n",  /* SID:72 */
    [LA_F73] = "[UDP] %s 向 %s:%d 发送，seq=0，flags=0x%02x，len=%d，retries=%d\n",  /* SID:73 */
    [LA_F74] = "% 网络初始化失败\n",  /* SID:74 */
    [LA_F75] = "探测 UDP 绑定失败(%d)\n",  /* SID:75 */
    [LA_F76] = "select 失败(%d)\n",  /* SID:76 */
};

static inline int lang_cn(void) {
    return lang_load(LA_RID, s_lang_cn, LA_NUM);
}
