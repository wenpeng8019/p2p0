#!/usr/bin/env python3
"""fix_la_arity.py — 为单参数 LA_W/S/F("str") 补上占位 ID 0: LA_W("str", 0)"""
import re
import os

files = [
    'src/p2p.c', 'src/p2p_ice.c', 'src/p2p_nat.c', 'src/p2p_route.c',
    'src/p2p_signal_compact.c', 'src/p2p_signal_pubsub.c', 'src/p2p_signal_relay.c',
    'src/p2p_stun.c', 'src/p2p_tcp_punch.c', 'src/p2p_trans_mbedtls.c',
    'src/p2p_trans_openssl.c', 'src/p2p_trans_pseudotcp.c', 'src/p2p_trans_reliable.c',
    'src/p2p_trans_sctp.c', 'src/p2p_turn.c', 'src/p2p_internal.h',
]

# Match LA_W/S/F("str") — string then ) with no comma before )
# Must NOT already have a second argument
pat = re.compile(r'LA_([WSF])\("((?:[^"\\]|\\.)*)"\s*\)')

repo = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(repo)  # go up from tools/

total = 0
for rel in files:
    path = os.path.join(repo, rel)
    with open(path, encoding='utf-8') as fh:
        txt = fh.read()

    def repl(m):
        return 'LA_{}("{}", 0)'.format(m.group(1), m.group(2))

    new = pat.sub(repl, txt)
    if new != txt:
        n = len(pat.findall(txt))
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(new)
        print(f'  {rel}: +{n} placeholders')
        total += n
    else:
        print(f'  {rel}: already correct')

print(f'\nTotal: {total} placeholders added')
