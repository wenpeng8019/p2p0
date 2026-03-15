//
// Created by 温朋 on 2026/3/14.
//

#ifndef P2P_INSTRUMENT_H
#define P2P_INSTRUMENT_H

#include <stdc.h>

enum {
    P2P_INST_OPT_SRFLX_PUNCH_OFF,           // 关闭公网后续地址打洞，只进行内网地址打洞（仅测试内网直连）
    P2P_INST_OPT_HOST_PUNCH_OFF,            // 关闭本地 Host 候选打洞（防止内网直连，仅通过公网地址打洞）
    P2P_INST_OPT_RELAY_OFF,                 // 关闭中继路径（即使 NAT 穿透失败也不降级中继，直接断开）
    P2P_INST_OPT_ICE_HOST_OFF,              // 关闭 ICE Host 候选（不收集本地网卡地址，测试仅使用 Srflx/Relay）
    P2P_INST_OPT_ICE_SRFLX_OFF,             // 关闭 ICE Srflx 候选（不收集 NAT 反射地址，测试仅使用 Host/Relay）
    P2P_INST_OPT_ICE_RELAY_OFF,             // 关闭 ICE Relay 候选（不收集 TURN 中继地址，测试仅使用 Host/Srflx）
};

void p2p_instrument(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len);

#endif //P2P_INSTRUMENT_H
