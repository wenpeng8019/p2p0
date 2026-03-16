//
// Created by 温朋 on 2026/3/14.
//
#include "p2p_instrument.h"

void p2p_instrument(uint16_t rid, uint8_t chn, const char* tag, char *txt, int len) {

    // 忽略自己的协同、以及 'X' 通道以外的数据
    if (!rid || chn != 'X') return;

    if (strcmp(tag, "host_ice") == 0) {
        
    }
}
