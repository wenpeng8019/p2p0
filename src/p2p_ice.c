/*
 * ICE 协议实现（RFC 5245 / RFC 8445）
 *
 * ============================================================================
 * 模块功能：协议转换层
 * ============================================================================
 *
 * 本模块作为**协议转换层**，提供项目内部格式与 ICE 标准格式的互转能力：
 *
 *   【核心能力】
 *   1. 候选地址收集（Gathering）
 *      - Host Candidate: 本地网卡地址
 *      - Server Reflexive: STUN 反射地址
 *      - Relay: TURN 中继地址
 *
 *   2. 候选交换（Trickle ICE）
 *      - Host 候选：收集后立即通过 Trickle ICE 逐个发送
 *      - Srflx/Relay 候选：收到 STUN/TURN 响应后立即发送
 *      - 批量重发：p2p_update() 定期检查并发送未确认的候选（断点续传）
 *      - 离线缓存：服务器缓存候选，等待对端上线后自动推送
 *
 *   3. 协议转换（WebRTC 互操作）★
 *      - SDP 导出：p2p_ice_export_sdp() - 将内部候选转为 SDP 格式
 *      - SDP 导入：p2p_import_ice_sdp() - 解析 SDP 候选为内部格式
 *      - STUN 构造：p2p_ice_build_connectivity_check() - 生成 ICE 标准 STUN 包
 *      - Priority 算法：p2p_ice_calc_priority() - RFC 5245 标准优先级计算
 *
 *   【NAT 模块负责连通性检查】
 *   - 连通性检查执行：NAT 模块遍历候选并发送探测包
 *   - 协议选择：自定义（PUNCH/REACH）或 ICE-STUN（调用本模块）
 *   - 路径管理：Priority 排序、超时重试、路径切换
 *
 * ============================================================================
 * 设计理念
 * ============================================================================
 *
 *   【项目内部对接】自定义高效协议
 *     - 候选格式：p2p_local_candidate_entry_t（7字节紧凑）
 *     - 信令协议：P2P_PKT_OFFER / P2P_PKT_ANSWER（自定义二进制）
 *     - 探测协议：P2P_PKT_PUNCH / REACH（2字节头部，无认证开销）
 *     - 不需要：SDP 文本解析、STUN 认证计算
 *
 *   【WebRTC 互操作】标准 ICE 协议
 *     - 候选格式：SDP a=candidate 行（ASCII 文本，约100字节/个）
 *     - 信令协议：WebRTC SDP offer/answer（JSON 包装）
 *     - 探测协议：STUN Binding Request（包含 USERNAME、MI、PRIORITY 等）
 *     - 完全符合：RFC 5245 标准，可与浏览器互通
 *
 *   本模块提供双模式支持，由上层配置决定使用哪种协议。
 *
 * 注：本实现支持对端离线时的候选缓存，详见 p2p_ice.h 中
 *    "与标准 ICE 的差异：离线候选缓存"章节。
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#define MOD_TAG "ICE"

#include "p2p_internal.h"

/* p2p_cand_type_t → p2p_ice_cand_type_t（值完全相同，可以强转） */
static inline p2p_ice_cand_type_t cand_type_to_ice(p2p_cand_type_t t) {
    return (p2p_ice_cand_type_t)t;
}

/* ============================================================================
 * 优先级计算（RFC 5245 Section 4.1.2）
 * ============================================================================
 *
 * 候选优先级公式：
 *   priority = (2^24) * type_preference +
 *              (2^8)  * local_preference +
 *              (2^0)  * (256 - component_id)
 *
 * 类型偏好值（RFC 5245 Section 4.1.2.2）：
 *   - Host:  126 （本地直连，最优先）
 *   - Prflx: 110 （对端反射，次优先）
 *   - Srflx: 100 （服务器反射，中等）
 *   - Relay:   0 （中继，最后选择）
 *
 * 本地偏好值用于区分同类型的多个候选（如多网卡）：
 *   - 通常设为 65535 减去网卡索引
 *   - 或基于网卡速度/稳定性排序
 *
 * 组件 ID：
 *   - RTP = 1
 *   - RTCP = 2（如果使用 RTCP-mux 则只有 RTP）
 */

/* 类型偏好值常量 */
#define ICE_TYPE_PREF_HOST   126
#define ICE_TYPE_PREF_PRFLX  110
#define ICE_TYPE_PREF_SRFLX  100
#define ICE_TYPE_PREF_RELAY    0

/*
 * 计算候选优先级
 *
 * @param type        候选类型
 * @param local_pref  本地偏好值（0-65535）
 * @param component   组件 ID（通常为 1）
 * @return            32 位优先级值
 */
uint32_t p2p_ice_calc_priority(p2p_ice_cand_type_t type, uint16_t local_pref, uint8_t component) {
    uint32_t type_pref;
    
    /* 根据候选类型确定类型偏好值 */
    switch (type) {
        case P2P_ICE_CAND_HOST:
            type_pref = ICE_TYPE_PREF_HOST;
            break;
        case P2P_ICE_CAND_PRFLX:
            type_pref = ICE_TYPE_PREF_PRFLX;
            break;
        case P2P_ICE_CAND_SRFLX:
            type_pref = ICE_TYPE_PREF_SRFLX;
            break;
        case P2P_ICE_CAND_RELAY:
            type_pref = ICE_TYPE_PREF_RELAY;
            break;
        default:
            type_pref = 0;
            break;
    }
    
    /*
     * 优先级计算：
     *   priority = (2^24) * type_pref + (2^8) * local_pref + (256 - component)
     *
     * 示例（Host, local_pref=65535, component=1）：
     *   = 16777216 * 126 + 256 * 65535 + 255
     *   = 2113929216 + 16776960 + 255
     *   = 2130706431
     */
    return ((uint32_t)type_pref << 24) + 
           ((uint32_t)local_pref << 8) + 
           (256 - component);
}

/* ============================================================================
 * 候选对优先级计算（RFC 5245 Section 5.7.2）
 * ============================================================================
 *
 * 候选对优先级用于确定连通性检查的顺序。
 *
 * 公式：
 *   pair_priority = 2^32 * MIN(G, D) + 2 * MAX(G, D) + (G > D ? 1 : 0)
 *
 * 其中：
 *   - G = controlling 端候选的优先级
 *   - D = controlled 端候选的优先级
 *
 * 这个公式确保：
 *   1. 高优先级候选对先被检查
 *   2. controlling 端优先时有轻微优势（+1）
 */

/*
 * 计算候选对优先级
 *
 * @param controlling_prio  controlling 端候选优先级
 * @param controlled_prio   controlled 端候选优先级
 * @param is_controlling    本端是否为 controlling 角色
 * @return                  64 位候选对优先级
 */
uint64_t p2p_ice_calc_pair_priority(uint32_t controlling_prio, uint32_t controlled_prio, int is_controlling) {
    uint64_t g, d;              /* g = controlling, d = controlled */
    uint64_t min_val, max_val;
    
    if (is_controlling) {
        /* 本端是 controlling，本地候选是 G */
        g = controlling_prio;
        d = controlled_prio;
    } else {
        /* 本端是 controlled，远端候选是 G */
        g = controlled_prio;
        d = controlling_prio;
    }
    
    /* 计算 MIN 和 MAX */
    min_val = (g < d) ? g : d;
    max_val = (g > d) ? g : d;
    
    /*
     * pair_priority = 2^32 * MIN(G, D) + 2 * MAX(G, D) + (G > D ? 1 : 0)
     *
     * 使用 64 位整数避免溢出：
     *   2^32 * 2130706431 (max priority) 需要 63 位
     */
    return (min_val << 32) + (max_val << 1) + (g > d ? 1 : 0);
}

/* ============================================================================
 * 检查列表生成与排序（RFC 5245 Section 5.7）
 * ============================================================================
 *
 * 检查列表（Check List）是所有候选对的有序列表，按优先级降序排列。
 *
 * 生成步骤：
 *   1. 将每个本地候选与每个远端候选配对
 *   2. 计算每个候选对的优先级
 *   3. 按优先级降序排序
 *   4. 剪枝（去除冗余候选对）
 *
 * 初始状态：
 *   - 第一个候选对设为 Waiting
 *   - 其余候选对设为 Frozen（等待前一个完成）
 */

/*
 * pair_compare: 候选对比较函数（用于 qsort）
 *
 * 按优先级降序排列（优先级高的在前）
 */
// static int pair_compare(const void *a, const void *b) {
//     const p2p_candidate_pair_t *pa = (const p2p_candidate_pair_t *)a;
//     const p2p_candidate_pair_t *pb = (const p2p_candidate_pair_t *)b;
    
//     /* 降序排列：优先级高的在前 */
//     if (pa->pair_priority > pb->pair_priority) return -1;
//     if (pa->pair_priority < pb->pair_priority) return 1;
//     return 0;
// }

/*
 * 生成候选对检查列表
 *
 * 将本地候选和远端候选组合成候选对，计算优先级并排序。
 *
 * @param pairs          输出：候选对数组
 * @param max_pairs      数组最大容量
 * @param local_cands    本地候选数组
 * @param local_cnt      本地候选数量
 * @param remote_cands   远端候选数组
 * @param remote_cnt     远端候选数量
 * @param is_controlling 本端是否为 controlling 角色
 * @return               生成的候选对数量
 */
// static int p2p_ice_form_check_list(
//     p2p_candidate_pair_t *pairs, int max_pairs,
//     const p2p_local_candidate_entry_t *local_cands, int local_cnt,
//     const p2p_local_candidate_entry_t *remote_cands, int remote_cnt,
//     int is_controlling
// ) {
//     int pair_cnt = 0;
    
//     /* 遍历所有本地和远端候选的组合 */
//     for (int i = 0; i < local_cnt && pair_cnt < max_pairs; i++) {
//         for (int j = 0; j < remote_cnt && pair_cnt < max_pairs; j++) {
//             p2p_candidate_pair_t *p = &pairs[pair_cnt];
            
//             /* 复制候选信息 */
//             p->local = local_cands[i];
//             p->remote = remote_cands[j];
            
//             /* 计算候选对优先级 */
//             if (is_controlling) {
//                 /* 本端是 controlling：本地候选是 G，远端候选是 D */
//                 p->pair_priority = p2p_ice_calc_pair_priority(
//                     local_cands[i].priority,
//                     remote_cands[j].priority,
//                     1
//                 );
//             } else {
//                 /* 本端是 controlled：远端候选是 G，本地候选是 D */
//                 p->pair_priority = p2p_ice_calc_pair_priority(
//                     remote_cands[j].priority,
//                     local_cands[i].priority,
//                     0
//                 );
//             }
            
//             /* 初始状态设为 Frozen */
//             p->state = P2P_PAIR_FROZEN;
//             p->nominated = 0;
//             p->last_check_time = 0;
//             p->check_count = 0;
            
//             pair_cnt++;
//         }
//     }
    
//     /* 按优先级降序排序 */
//     if (pair_cnt > 1) {
//         qsort(pairs, pair_cnt, sizeof(p2p_candidate_pair_t), pair_compare);
//     }
    
//     /* 第一个候选对设为 Waiting（可以开始检查） */
//     if (pair_cnt > 0) {
//         pairs[0].state = P2P_PAIR_WAITING;
//     }
    
//     print("I:", LA_F("Formed check list with %d candidate pairs", LA_F255, 255), pair_cnt);
//     for (int i = 0; i < pair_cnt && i < 5; i++) {  /* 只打印前 5 个 */
//         print("I:", LA_F("  [%d] L=%s:%d -> R=%s:%d, pri=0x%016llx", LA_F43, 43),
//                i,
//                inet_ntoa(pairs[i].local.addr.sin_addr),
//                ntohs(pairs[i].local.addr.sin_port),
//                inet_ntoa(pairs[i].remote.addr.sin_addr),
//                ntohs(pairs[i].remote.addr.sin_port),
//                (unsigned long long)pairs[i].pair_priority);
//     }
//     if (pair_cnt > 5) {
//         print("I:", LA_F("  ... and %d more pairs", LA_F41, 41), pair_cnt - 5);
//     }
    
//     return pair_cnt;
// }

/* ============================================================================
 * 协议转换层实现（WebRTC 互操作）
 * ============================================================================
 *
 * 本模块提供 SDP 格式转换能力，用于与 WebRTC 互操作。
 *
 * ============================================================================
 * SDP Candidate 格式说明（RFC 5245）
 * ============================================================================
 *
 * 【标准格式】
 *   a=candidate:<foundation> <component> <protocol> <priority> <ip> <port> 
 *               typ <type> [raddr <rel-addr>] [rport <rel-port>]
 *
 * 【字段说明】
 *   - foundation: 候选基础标识（相同基础地址的候选使用相同值）
 *   - component:  组件ID（1=RTP, 2=RTCP，本实现固定为1）
 *   - protocol:   传输协议（本实现固定为 UDP）
 *   - priority:   候选优先级（RFC 5245 公式计算）
 *   - ip/port:    候选地址
 *   - type:       候选类型（host / srflx / relay / prflx）
 *   - raddr/rport: 关联地址（仅 srflx/relay，指向基础地址）
 *
 * 【示例】
 *   Host 候选:
 *     a=candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host\r\n
 *
 *   Srflx 候选（包含 raddr/rport）:
 *     a=candidate:2 1 UDP 1694498815 203.0.113.77 54320 typ srflx raddr 192.168.1.10 rport 54320\r\n
 *
 * ============================================================================
 * C 代码与 WebRTC JavaScript 的格式差异
 * ============================================================================
 *
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ API                           │ 格式                                  │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ p2p_ice_export_candidate()    │ WebRTC 候选字符串                     │
 * │ (单个候选)                    │ "candidate:1 1 UDP 2130706431 ..."    │
 * │                               │ ❌ 无 "a=" 前缀                        │
 * │                               │ ❌ 无 "\r\n" 后缀                      │
 * │                               │ ✅ Trickle ICE 模式（逐个发送）         │
 * │                               │ ✅ 对应 event.candidate.candidate      │
 * ├──────────────────────────────────────────────────────────────────────┤
 * │ p2p_ice_export_sdp()          │ 完整 SDP 行（带前缀和后缀）            │
 * │ (批量候选)                    │ "a=candidate:1 1 UDP 2130706431...\r\n"│
 * │                               │ ✅ 包含 "a=" 前缀                       │
 * │                               │ ✅ 包含 "\r\n" 行终止符                 │
 * │                               │ ✅ 可批量输出多行（多个候选）            │
 * │                               │ ✅ 对应 SDP 文本中的 a 行               │
 * └──────────────────────────────────────────────────────────────────────┘
 *
 * 【核心差异】
 *   • p2p_ice_export_candidate():   输出纯候选字符串，直接用于 WebRTC
 *   • p2p_ice_export_sdp():         输出 SDP 行，用于嵌入完整 SDP
 *   • foundation 之后的内容完全相同
 *
 * ============================================================================
 * WebRTC SDP 完整结构（pc.createOffer() 返回内容）
 * ============================================================================
 *
 * 【重要】pc.createOffer() 返回的是**完整 SDP 文本**，远不止候选地址！
 *
 * SDP (Session Description Protocol) 包含：
 *   1. 会话级信息（版本、源、时间）
 *   2. ICE 凭证（ice-ufrag, ice-pwd）
 *   3. 媒体描述（音频/视频/数据通道）
 *   4. 编解码器列表（opus, VP8, H.264 等）
 *   5. DTLS 指纹（用于加密）
 *   6. **ICE 候选列表**（a=candidate: 行）
 *
 * 【完整 SDP 示例】
 * ```sdp
 * v=0                                              # SDP 版本
 * o=- 123456789 2 IN IP4 127.0.0.1                # 会话源
 * s=-                                              # 会话名称
 * t=0 0                                            # 时间描述
 * a=group:BUNDLE 0                                 # Bundle 策略
 * a=msid-semantic: WMS                             # Media Stream 语义
 * 
 * m=application 9 UDP/DTLS/SCTP webrtc-datachannel # 媒体类型
 * c=IN IP4 0.0.0.0                                 # 连接信息
 * a=ice-ufrag:aB3d                                 # ★ ICE 用户名片段
 * a=ice-pwd:Xy7zK9mN2pQ4rS5tU6vW8x                # ★ ICE 密码
 * a=ice-options:trickle                            # 支持 Trickle ICE
 * a=fingerprint:sha-256 12:34:56:...               # DTLS 指纹
 * a=setup:actpass                                  # DTLS 角色协商
 * a=mid:0                                          # 媒体流 ID
 * a=sctp-port:5000                                 # SCTP 端口
 * a=max-message-size:262144                        # 最大消息大小
 * 
 * # ========== ICE 候选列表（可能为空或部分） ==========
 * a=candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host
 * a=candidate:2 1 UDP 1694498815 203.0.113.77 54320 typ srflx raddr 192.168.1.10 rport 54320
 * a=candidate:3 1 UDP 16777215 10.0.0.1 3478 typ relay raddr 203.0.113.77 rport 54320
 * # ... 更多候选
 * ```
 *
 * 【候选列表的时序】
 *
 *   场景 1: 等待收集完成后再 createOffer()
 *     - 调用 createOffer() 前已完成候选收集
 *     - offer SDP 包含所有已知候选（批量模式）
 *     - 对端收到 offer 后立即开始连通性检查
 *     - 优点: 减少信令轮次
 *     - 缺点: 增加首包延迟（需等待 STUN/TURN 响应）
 *
 *   场景 2: 立即 createOffer()（Trickle ICE）
 *     - 调用 createOffer() 时候选收集尚未完成
 *     - offer SDP 可能只包含 host 候选，或完全没有候选
 *     - 通过 onicecandidate 事件增量发送后续候选
 *     - 优点: 降低 50-80% 首包延迟
 *     - 缺点: 增加信令轮次（每个候选一次）
 *
 * 【关键字段说明】
 *
 *   ice-ufrag / ice-pwd:
 *     - 用于 STUN 消息认证（MESSAGE-INTEGRITY）
 *     - 对端发送连通性检查时，USERNAME = "本端ufrag:对端ufrag"
 *     - 本模块的 p2p_ice_build_connectivity_check() 会使用这些凭证
 *
 *   a=candidate: 行:
 *     - 每个候选一行（本模块的 p2p_ice_export_sdp() 生成）
 *     - 对端调用 p2p_ice_import_sdp() 解析
 *
 * 【本项目如何处理完整 SDP】
 *
 *   与 WebRTC 互操作时，完整流程：
 *
 *   1. 生成 offer SDP:
 *      - ICE 凭证: 项目需自己生成随机 ufrag/pwd（本模块不负责）
 *      - 候选列表: 调用 p2p_ice_export_sdp() 生成 a=candidate: 行
 *      - 其他字段: 媒体描述、DTLS 指纹等（项目需自己构造）
 *
 *   2. 解析 answer SDP:
 *      - 提取 ice-ufrag/ice-pwd: 用于后续 STUN 认证
 *      - 提取候选: 调用 p2p_ice_import_sdp() 解析 a=candidate: 行
 *      - 其他字段: 媒体协商结果（项目需自己处理）
 *
 *   3. 发送连通性检查:
 *      - 使用 p2p_ice_build_connectivity_check() 构造 STUN 包
 *      - 传入双方的 ufrag/pwd（从 SDP 获取）
 *      - NAT 模块负责发送和接收
 *
 * 【与本模块的关系】
 *
 *   p2p_ice 模块**仅负责候选相关的部分**：
 *     ✅ p2p_ice_export_sdp():   生成 a=candidate: 行（插入完整 SDP）
 *     ✅ p2p_ice_import_sdp():   解析 a=candidate: 行（从完整 SDP 提取）
 *     ✅ p2p_ice_calc_priority(): 计算候选优先级
 *     ✅ p2p_ice_build_connectivity_check(): 生成 ICE-STUN 包
 *
 *   完整 SDP 的其他部分（ice-ufrag, 媒体描述等）由项目其他模块负责。
 *
 * ============================================================================
 * C ↔ WebRTC 双向集成
 * ============================================================================
 *
 * 【C → WebRTC】发送候选给浏览器
 *
 *   方式 1: Trickle ICE（推荐）- 逐个发送，降低延迟
 *   ```c
 *   // 每收集一个候选立即发送
 *   char candidate_str[256];
 *   int len = p2p_ice_export_candidate(&s->local_cands[idx], 
 *                                       candidate_str, sizeof(candidate_str));
 *   if (len > 0) {
 *       // 直接发送给 WebRTC，无需处理前缀后缀
 *       send_to_webrtc_via_websocket(candidate_str);
 *   }
 *   ```
 *
 *   方式 2: 批量模式 - 一次性发送所有候选（需分割）
 *   ```c
 *   char sdp_buf[2048];
 *   int len = p2p_ice_export_sdp(s->local_cands, s->local_cand_cnt, 
 *                                  sdp_buf, sizeof(sdp_buf), 
 *                                  0, NULL, NULL, NULL);  // 仅候选
 *   
 *   // 遍历每一行，去除 "a=" 前缀和 "\r\n" 后缀
 *   char *line = sdp_buf;
 *   while (*line) {
 *       if (strncmp(line, "a=", 2) == 0) {
 *           char *end = strchr(line, '\r');
 *           if (end) *end = '\0';
 *           send_to_webrtc(line + 2);  // 跳过 "a="
 *       }
 *       // 移动到下一行
 *       while (*line && *line != '\n') line++;
 *       if (*line == '\n') line++;
 *   }
 *   ```
 *
 *   方式 3: 完整 SDP - 生成包含所有字段的 SDP offer/answer
 *   ```c
 *   char sdp_buf[4096];
 *   const char *ice_ufrag = "aB3d";
 *   const char *ice_pwd = "Xy7zK9pLm3nO1qW2aBcD";
 *   const char *dtls_fingerprint = "sha-256 AB:CD:EF:01:23:45:67:89";
 *   
 *   int len = p2p_ice_export_sdp(s->local_cands, s->local_cand_cnt, 
 *                                  sdp_buf, sizeof(sdp_buf), 
 *                                  1,                 // generate_full=1
 *                                  ice_ufrag, 
 *                                  ice_pwd, 
 *                                  dtls_fingerprint); // 可选
 *   
 *   if (len > 0) {
 *       // sdp_buf 包含完整的 SDP offer，可直接发送
 *       send_sdp_to_webrtc(sdp_buf);
 *   }
 *   ```
 *
 * 【WebRTC → C】接收浏览器候选
 *
 *   1. 从信令通道接收 WebRTC candidate 字符串
 *   2. 添加 "a=" 前缀和 "\r\n" 后缀
 *   3. 调用 p2p_ice_import_sdp() 解析为 C 内部格式
 *
 *   示例代码：
 *   ```javascript
 *   // JavaScript (浏览器端)
 *   pc.onicecandidate = (event) => {
 *       if (event.candidate) {
 *           const candidateStr = event.candidate.candidate;
 *           // candidateStr 格式: "candidate:1 1 UDP 2130706431 ..."
 *           
 *           // 发送给 C 端（通过 WebSocket）
 *           websocket.send(JSON.stringify({
 *               type: 'ice-candidate',
 *               candidate: candidateStr
 *           }));
 *       }
 *   };
 *   ```
 *
 *   ```c
 *   // C 代码（接收端）
 *   void on_websocket_message(const char *json_msg) {
 *       // 解析 JSON，提取 candidate 字符串
 *       const char *candidate = extract_candidate_from_json(json_msg);
 *       
 *       // 添加 "a=" 前缀和 "\r\n" 后缀
 *       char sdp_line[512];
 *       snprintf(sdp_line, sizeof(sdp_line), "a=%s\r\n", candidate);
 *       
 *       // 解析为内部格式
 *       p2p_remote_candidate_entry_t remote_cand;
 *       if (p2p_import_ice_sdp(sdp_line, &remote_cand, 1) > 0) {
 *           // 添加到远端候选列表
 *           p2p_add_remote_candidate(s, &remote_cand);
 *       }
 *   }
 *   ```
 *
 * ============================================================================
 * 批量模式 vs Trickle ICE 模式
 * ============================================================================
 *
 * 【批量模式】
 *   - 使用 p2p_ice_export_sdp() 一次性导出所有候选
 *   - 输出多行 "a=candidate:..." 字符串
 *   - 适合传统 SDP offer/answer 交换
 *   - 需要等待候选收集完成
 *   - 首包延迟较高（需等待 STUN/TURN 响应）
 *
 * 【Trickle ICE 模式】（RFC 8838，推荐）
 *   - 使用 p2p_ice_export_candidate() 逐个导出候选
 *   - 每收集一个候选立即发送（无需等待收集完成）
 *   - WebRTC 标准模式（pc.onicecandidate 逐个触发）
 *   - 降低 50-80% 首包延迟
 *   - 本项目支持：通过 nat_punch(s, idx) 逐个打洞
 *
 * 【完整 SDP 模式】
 *   - 使用 p2p_ice_export_sdp() 并设置 generate_full=1
 *   - 输出包含 v=, o=, s=, t=, m=, ice-ufrag, ice-pwd, fingerprint, candidates
 *   - 适合与 WebRTC 进行完整 SDP offer/answer 交换
 *   - 无需手动拼接 SDP 各部分
 *
 * 【实现对比】
 *
 *   C Trickle 模式（推荐）:
 *     ```c
 *     // 每收集一个候选调用一次
 *     char buf[256];
 *     int len = p2p_export_ice_candidate(&s->local_cands[idx], buf, sizeof(buf));
 *     send_to_webrtc(buf);  // 直接发送，无需处理前缀
 *     ```
 *
 *   C 批量模式（仅候选）:
 *     ```c
 *     // 一次导出所有候选
 *     char buf[2048];
 *     int len = p2p_ice_export_sdp(s->local_cands, cnt, buf, sizeof(buf),
 *                                    0, NULL, NULL, NULL);  // 仅候选
 *     // 需要分割并去除 "a=" 前缀，然后逐个发送
 *     ```
 *
 *   C 完整 SDP 模式:
 *     ```c
 *     // 生成完整 SDP
 *     char buf[4096];
 *     int len = p2p_export_ice_sdp(s->local_cands, cnt, buf, sizeof(buf),
 *                                          1,           // generate_full=1
 *                                          "aB3d",      // ice_ufrag
 *                                          "Xy7zK9...", // ice_pwd
 *                                          "sha-256 AB:CD:...");  // DTLS fingerprint
 *     // buf 包含完整 SDP，可直接发送
 *     ```
 *
 *   WebRTC 端（Trickle 接收）:
 *     ```javascript
 *     websocket.onmessage = (event) => {
 *         const data = JSON.parse(event.data);
 *         if (data.type === 'ice-candidate') {
 *             // 创建 RTCIceCandidate 对象
 *             const candidate = new RTCIceCandidate({
 *                 candidate: data.candidate,  // "candidate:1 1 UDP ..."
 *                 sdpMid: '0',
 *                 sdpMLineIndex: 0
 *             });
 *             pc.addIceCandidate(candidate);
 *         }
 *     };
 *     ```
 *
 * ============================================================================
 */

/*
 * 导出单个候选为 WebRTC 格式（无 a= 前缀和 \r\n 后缀）
 *
 * 对应 WebRTC event.candidate.candidate 字符串格式。
 * 用于 Trickle ICE 模式，每收集一个候选立即发送。
 *
 * 输出格式："candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host"
 */
int p2p_ice_export_candidate(const p2p_local_candidate_entry_t *cand, char *buf, int buf_size ) {

    if (!cand || !buf || buf_size < 100) return -1;
    
    /* 候选类型字符串映射 */
    const char *type_str;
    switch (cand->type) {
        case P2P_CAND_HOST:   type_str = "host"; break;
        case P2P_CAND_SRFLX:  type_str = "srflx"; break;
        case P2P_CAND_RELAY:  type_str = "relay"; break;
        case P2P_CAND_PRFLX:  type_str = "prflx"; break;
        default:              type_str = "host"; break;
    }
    
    /* 格式化候选字符串（无 a= 前缀，无 \r\n 后缀）*/
    int n;
    if (cand->type == P2P_CAND_SRFLX || cand->type == P2P_CAND_RELAY) {
        /* Srflx/Relay: 包含 raddr/rport (base_addr) */
        /* inet_ntoa 使用静态缓冲区，不能在同一调用中使用两次 */
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cand->addr.sin_addr, addr_str, sizeof(addr_str));
        char raddr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cand->base_addr.sin_addr, raddr_str, sizeof(raddr_str));
        n = snprintf(buf, buf_size,
            "candidate:1 1 UDP %u %s %d typ %s raddr %s rport %d",
            cand->priority,                        /* priority */
            addr_str,                              /* IP */
            ntohs(cand->addr.sin_port),            /* port */
            type_str,                              /* type */
            raddr_str,                             /* related address */
            ntohs(cand->base_addr.sin_port)        /* related port */
        );
    } else {
        /* Host/Prflx: 不包含 raddr/rport */
        n = snprintf(buf, buf_size,
            "candidate:1 1 UDP %u %s %d typ %s",
            cand->priority,                        /* priority */
            inet_ntoa(cand->addr.sin_addr),        /* IP */
            ntohs(cand->addr.sin_port),            /* port */
            type_str                               /* type */
        );
    }
    
    if (n < 0 || n >= buf_size) {
        print("E:", LA_F("WebRTC candidate export buffer overflow", LA_F424, 424));
        return -1;
    }
    
    return n;
}

/*
 * 导出多个候选为 SDP 格式（带 a= 前缀和 \r\n 后缀）
 *
 * 【重要】本函数可以生成完整的 SDP offer/answer 文本。
 *
 * 用途 1: 仅生成候选部分（嵌入到已有 SDP）
 * 用途 2: 生成完整 SDP 文本（包含 v=, o=, s=, t=, m=, ice-ufrag 等）
 *
 * 完整 SDP 示例：
 *   v=0
 *   o=- 123456789 2 IN IP4 127.0.0.1
 *   s=-
 *   t=0 0
 *   m=application 9 UDP/DTLS/SCTP webrtc-datachannel
 *   c=IN IP4 0.0.0.0
 *   a=ice-ufrag:aB3d                    ← 由调用者提供
 *   a=ice-pwd:Xy7zK9pLm3nO1qW2         ← 由调用者提供
 *   a=fingerprint:sha-256 AB:CD...      ← 可选，由 DTLS 模块提供
 *   a=candidate:1 1 UDP 2130706431...   ← 本函数生成
 *   a=candidate:2 1 UDP 1694498815...   ← 本函数生成
 *
 * SDP candidate 格式 (RFC 5245)：
 *   a=candidate:<foundation> <component> <protocol> <priority> <ip> <port> 
 *               typ <type> [raddr <rel-addr>] [rport <rel-port>]
 *
 * Foundation: 相同基础地址的候选使用相同的 foundation
 *             简化实现：使用候选索引作为 foundation
 * Component:  固定为 1（RTP，不使用 RTCP）
 * Protocol:   固定为 UDP
 * Type:       host / srflx / relay / prflx
 *
 * @param cands             本地候选数组
 * @param cnt               候选数量
 * @param sdp_buf           输出缓冲区
 *                          - 若只生成候选：每个约 100 字节
 *                          - 若生成完整 SDP：需要额外 500-1000 字节（建议 >= 2048）
 * @param buf_size          缓冲区大小
 * @param generate_full     1=生成完整 SDP, 0=仅生成候选行
 * @param ice_ufrag         ICE 用户名片段（仅 generate_full=1 时需要，否则传 NULL）
 * @param ice_pwd           ICE 密码（仅 generate_full=1 时需要，否则传 NULL）
 * @param dtls_fingerprint  DTLS 指纹（可选，格式："sha-256 AB:CD:..."，可传 NULL）
 *
 * @return 生成的 SDP 字符串总长度，失败返回 -1
 *
 * 使用示例：
 *   // 方式1: 仅候选（嵌入已有 SDP）
 *   int len = p2p_ice_export_sdp(cands, cnt, buf, size, 0, NULL, NULL, NULL);
 *
 *   // 方式2: 完整 SDP（自包含格式）
 *   int len = p2p_ice_export_sdp(cands, cnt, buf, size, 1,
 *                                        "aB3d", "Xy7zK9pLm3nO1qW2", 
 *                                        "sha-256 AB:CD:EF:...");
 */
int p2p_ice_export_sdp(const p2p_local_candidate_entry_t *cands, int cnt,
                       char *sdp_buf, int buf_size, bool candidates_only,
                       const char *ice_ufrag,
                       const char *ice_pwd,
                       const char *dtls_fingerprint) {

    if (!cands || !sdp_buf || buf_size < 100) return -1;
    
    /* 参数校验 */
    if (!candidates_only) {
        if (!ice_ufrag || !ice_pwd) {
            print("E:", LA_F("Full SDP generation requires ice_ufrag and ice_pwd", LA_F298, 298));
            return -1;
        }
        if (buf_size < 2048) {
            print("W:", LA_F("Buffer size < 2048 may be insufficient for full SDP", LA_F266, 266));
        }
    }
    
    int offset = 0;
    
    /* 生成完整 SDP 头部（如果需要）*/
    if (!candidates_only) {
        /* 生成会话 ID（使用当前时间戳）*/
        uint32_t session_id = (uint32_t)time(NULL);
        
        /* Session Description (v=, o=, s=, t=) */
        int n = snprintf(sdp_buf + offset, buf_size - offset,
            "v=0\r\n"
            "o=- %u 2 IN IP4 127.0.0.1\r\n"
            "s=-\r\n"
            "t=0 0\r\n",
            session_id
        );
        if (n < 0 || offset + n >= buf_size) goto overflow;
        offset += n;
        
        /* Media Description (m=, c=) 
         * 使用 application 类型 + UDP/DTLS/SCTP，适配 WebRTC DataChannel */
        n = snprintf(sdp_buf + offset, buf_size - offset,
            "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
            "c=IN IP4 0.0.0.0\r\n"
        );
        if (n < 0 || offset + n >= buf_size) goto overflow;
        offset += n;
        
        /* ICE Credentials */
        n = snprintf(sdp_buf + offset, buf_size - offset,
            "a=ice-ufrag:%s\r\n"
            "a=ice-pwd:%s\r\n",
            ice_ufrag,
            ice_pwd
        );
        if (n < 0 || offset + n >= buf_size) goto overflow;
        offset += n;
        
        /* DTLS Fingerprint (可选) */
        if (dtls_fingerprint) {
            n = snprintf(sdp_buf + offset, buf_size - offset,
                "a=fingerprint:%s\r\n",
                dtls_fingerprint
            );
            if (n < 0 || offset + n >= buf_size) goto overflow;
            offset += n;
        }
        
        /* Setup attribute (默认为 actpass，允许双向连接) */
        n = snprintf(sdp_buf + offset, buf_size - offset, "a=setup:actpass\r\n");
        if (n < 0 || offset + n >= buf_size) goto overflow;
        offset += n;
    }
    
    /* 生成候选行 */
    for (int i = 0; i < cnt; i++) {
        const p2p_local_candidate_entry_t *c = &cands[i];
        
        /* 候选类型字符串映射 */
        const char *type_str;
        switch (c->type) {
            case P2P_CAND_HOST:   type_str = "host"; break;
            case P2P_CAND_SRFLX:  type_str = "srflx"; break;
            case P2P_CAND_RELAY:  type_str = "relay"; break;
            case P2P_CAND_PRFLX:  type_str = "prflx"; break;
            default:              type_str = "host"; break;
        }
        
        /* 格式化候选行 */
        int n;
        if (c->type == P2P_CAND_SRFLX || c->type == P2P_CAND_RELAY) {
            /* Srflx/Relay: 包含 raddr/rport (base_addr) */
            /* inet_ntoa 使用静态缓冲区，不能在同一调用中使用两次 */
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &c->addr.sin_addr, addr_str, sizeof(addr_str));
            char raddr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &c->base_addr.sin_addr, raddr_str, sizeof(raddr_str));
            n = snprintf(sdp_buf + offset, buf_size - offset,
                "a=candidate:%d 1 UDP %u %s %d typ %s raddr %s rport %d\r\n",
                i + 1,                              /* foundation */
                c->priority,                        /* priority */
                addr_str,                           /* IP */
                ntohs(c->addr.sin_port),            /* port */
                type_str,                           /* type */
                raddr_str,                          /* related address */
                ntohs(c->base_addr.sin_port)        /* related port */
            );
        } else {
            /* Host/Prflx: 不包含 raddr/rport */
            n = snprintf(sdp_buf + offset, buf_size - offset,
                "a=candidate:%d 1 UDP %u %s %d typ %s\r\n",
                i + 1,                              /* foundation */
                c->priority,                        /* priority */
                inet_ntoa(c->addr.sin_addr),        /* IP */
                ntohs(c->addr.sin_port),            /* port */
                type_str                            /* type */
            );
        }
        
        if (n < 0 || offset + n >= buf_size) goto overflow;
        offset += n;
    }
    
    print("I:", LA_F("Exported %d candidates to SDP (%d bytes)", LA_F278, 278), cnt, offset);
    return offset;

overflow:
    print("E:", LA_F("SDP export buffer overflow", LA_F368, 368));
    return -1;
}

/*
 * 从 SDP 解析远端候选
 *
 * 解析格式示例：
 *   a=candidate:1 1 UDP 2130706431 192.168.1.10 54320 typ host
 *   a=candidate:2 1 UDP 1694498815 203.0.113.77 54320 typ srflx raddr 192.168.1.10 rport 54320
 *
 * 简化实现：使用 sscanf 解析固定格式，不支持扩展属性
 */
int p2p_ice_import_sdp(const char *sdp_text, p2p_remote_candidate_entry_t *cands, int max_cands) {

    if (!sdp_text || !cands || max_cands <= 0) return -1;
    
    int count = 0;
    const char *line = sdp_text;
    
    while (*line && count < max_cands) {
        /* 跳过非候选行 */
        if (strncmp(line, "a=candidate:", 12) != 0) {
            /* 跳到下一行 */
            while (*line && *line != '\n' && *line != '\r') line++;
            while (*line == '\n' || *line == '\r') line++;
            continue;
        }
        
        /* 解析候选行 */
        p2p_remote_candidate_entry_t *c = &cands[count];
        
        char ip_str[64];
        char type_str[16];
        char rel_ip_str[64] = {0};
        int foundation, component, port, rel_port = 0;
        unsigned int priority;
        
        /* 基础字段解析 */
        int matched = sscanf(line, "a=candidate:%d %d UDP %u %63s %d typ %15s",
                            &foundation, &component, &priority, 
                            ip_str, &port, type_str);
        
        if (matched < 6) {
            print("W:", LA_F("Failed to parse SDP candidate line: %s", LA_F285, 285), line);
            /* 跳到下一行 */
            while (*line && *line != '\n' && *line != '\r') line++;
            while (*line == '\n' || *line == '\r') line++;
            continue;
        }
        
        /* 解析候选类型 */
        if (strcmp(type_str, "host") == 0) {
            c->type = P2P_CAND_HOST;
        } else if (strcmp(type_str, "srflx") == 0) {
            c->type = P2P_CAND_SRFLX;
            /* 尝试解析 raddr/rport（可选） */
            sscanf(line, "a=candidate:%*d %*d UDP %*u %*s %*d typ srflx raddr %63s rport %d",
                   rel_ip_str, &rel_port);
        } else if (strcmp(type_str, "relay") == 0) {
            c->type = P2P_CAND_RELAY;
        } else if (strcmp(type_str, "prflx") == 0) {
            c->type = P2P_CAND_PRFLX;
        } else {
            print("W:", LA_F("Unknown candidate type: %s", LA_F421, 421), type_str);
            /* 跳到下一行 */
            while (*line && *line != '\n' && *line != '\r') line++;
            while (*line == '\n' || *line == '\r') line++;
            continue;
        }
        
        /* 解析 IP 地址 */
        memset(&c->addr, 0, sizeof(c->addr));
        c->addr.sin_family = AF_INET;
        if (inet_pton(AF_INET, ip_str, &c->addr.sin_addr) != 1) {
            print("W:", LA_F("Invalid IP address: %s", LA_F314, 314), ip_str);
            /* 跳到下一行 */
            while (*line && *line != '\n' && *line != '\r') line++;
            while (*line == '\n' || *line == '\r') line++;
            continue;
        }
        c->addr.sin_port = htons((uint16_t)port);
        
        /* 设置优先级 */
        c->priority = priority;
        
        /* 初始化运行时状态 */
        c->last_punch_send_ms = 0;
        path_stats_init(&c->stats, 0);
        
        count++;
        
        print("I:", LA_F("Imported SDP candidate: %s:%d typ %s (priority=0x%08x)", LA_F309, 309),
               inet_ntoa(c->addr.sin_addr), ntohs(c->addr.sin_port), type_str, priority);
        
        /* 跳到下一行 */
        while (*line && *line != '\n' && *line != '\r') line++;
        while (*line == '\n' || *line == '\r') line++;
    }
    
    print("I:", LA_F("Imported %d candidates from SDP", LA_F308, 308), count);
    return count;
}

/*
 * 构建 ICE 标准的连通性检查包（包装 p2p_stun_build_ice_check）
 *
 * 将 ICE 凭证（ufrag/pwd）组合为 STUN 认证参数，调用底层 STUN 构建函数。
 */
int p2p_ice_build_connectivity_check(uint8_t *buf, int max_len,
                                     const char *local_ufrag, const char *local_pwd,
                                     const char *remote_ufrag, const char *remote_pwd,
                                     uint32_t priority, int is_controlling, 
                                     uint64_t tie_breaker, int use_candidate) {
                                        
    /* 构造 USERNAME 字段: "remote_ufrag:local_ufrag" */
    char username[256];
    if (remote_ufrag && local_ufrag) {
        snprintf(username, sizeof(username), "%s:%s", remote_ufrag, local_ufrag);
    } else {
        username[0] = '\0';
    }
    
    /* 调用 STUN 模块的 ICE 检查包构建函数 */
    return p2p_stun_build_ice_check(
        buf, max_len, NULL,
        username[0] ? username : NULL,
        remote_pwd,
        priority, is_controlling, tie_breaker, use_candidate
    );
}

#pragma clang diagnostic pop
