//
// Created by 温朋 on 2026/3/14.
//

#ifndef P2P_INSTRUMENT_H
#define P2P_INSTRUMENT_H

#include <stdint.h>

enum {
    P2P_INST_OPT_NAT_REACH_BACKWARD_OFF,                        /* 禁止直接原路返回 reach 包 */
    P2P_INST_OPT_NAT_REACH_FORWARD_OFF,                         /* 禁止从其他路径路由 reach 包 */
    P2P_INST_OPT_NAT_ALIVE_PUNCH_OFF,                           /* nat 进入 connected/relay 状态后，禁止发送 alive/punch 包 */
    P2P_INST_OPT_NAT_CONN_ACK_OFF,                              /* 禁止发送 conn 包的应答包 */
    P2P_INST_OPT_AUTO_PROBE_OFF,                                /* 关闭自动信道外探测机制 */
    P2P_INST_OPT_RETRY_OFF,                                     /* 禁止可靠性重试机制（避免长时断点调式时重复发包） */ 
    P2P_INST_OPT_TIMEOUT_OFF,                                   /* 禁止超时机制（避免长时断点调式导致超时） */
    P2P_INST_OPT_STEP_WAIT,                                     /* 每执行一个操作就进入等待（等待对端断点调试完成） */
};

void p2p_instrument(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len);

#endif //P2P_INSTRUMENT_H
