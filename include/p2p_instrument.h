//
// Created by 温朋 on 2026/3/14.
//

#ifndef P2P_INSTRUMENT_H
#define P2P_INSTRUMENT_H

#include <stdint.h>

enum {
    P2P_INST_OPT_ICE_HOST_OFF,              // 关闭 ICE Host 候选（不收集本地网卡地址，测试仅使用 Srflx/Relay）
    P2P_INST_OPT_ICE_SRFLX_OFF,             // 关闭 ICE Srflx 候选（不收集 NAT 反射地址，测试仅使用 Host/Relay）
    P2P_INST_OPT_ICE_RELAY_OFF,             // 关闭 ICE Relay 候选（不收集 TURN 中继地址，测试仅使用 Host/Srflx）
};

void p2p_instrument(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len);

#endif //P2P_INSTRUMENT_H
