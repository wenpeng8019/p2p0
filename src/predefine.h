//
// Created by 温朋 on 2026/3/17.
//

#ifndef P2P_PREDEFINE_H
#define P2P_PREDEFINE_H

#include <p2p.h>
#include <p2p_instrument.h>

#define LOG_CALLBACK ((void*)p2p_log_callback)
#define LOG_LEVEL ((unsigned)p2p_log_level)
#define LOG_TAG_P p2p_log_pre_tag

#define INSTRUMENT_OPT_BASE p2p_instrument_base

#include <stdc.h>

#endif //P2P_PREDEFINE_H
