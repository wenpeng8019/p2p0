#!/usr/bin/env python3
"""
migrate_lang.py — 将 src/ 中所有 MSG(MSG_XXX) 调用迁移到 i18nC LA_X() 宏

用法：
  python3 tools/migrate_lang.py [--dry-run]

规则：
  - 英文字符串含 %  → LA_F("...")
  - MSG(id) 作为 "%s" 参数   → LA_S("...")
  - 其余（单词/短语）         → LA_W("...")
"""

import re
import sys
import os

# ─── 1. 解析 p2p_lang.c 英文词表 ─────────────────────────────────────────────

def parse_en_table(lang_c_path: str) -> dict[str, str]:
    """返回 {MSG_XXX: "english string"} 映射"""
    with open(lang_c_path, encoding="utf-8") as f:
        text = f.read()

    # 找到 messages_en 数组区域（到下一个 }; 结束）
    m = re.search(r'static const char\* messages_en\[MSG_COUNT\]\s*=\s*\{(.*?)\};',
                  text, re.DOTALL)
    if not m:
        raise RuntimeError("找不到 messages_en 数组")

    body = m.group(1)
    mapping = {}
    # 匹配 [MSG_XXX] = "string"  （可能带多行注释、空格）
    for entry in re.finditer(r'\[(\w+)\]\s*=\s*"((?:[^"\\]|\\.)*)"', body):
        key, val = entry.group(1), entry.group(2)
        # 反转义转义序列，保留原始 C 字符串内容（\n → \\n 不展开，保持 C 字面量）
        mapping[key] = val
    return mapping

# ─── 2. 选择宏类型 ────────────────────────────────────────────────────────────

def choose_macro(en_str: str, context_before: str) -> str:
    """根据字符串内容和调用上下文选择 LA_F / LA_S / LA_W"""
    if '%' in en_str:
        return 'LA_F'
    # 如果调用处是 "%s", MSG(id) 模式 → LA_S
    # context_before 是 MSG(...) 前面的代码片段
    if re.search(r'"%s"\s*,\s*$', context_before.rstrip()):
        return 'LA_S'
    # 如果是多个 MSG 拼接在格式串里（前面也是 MSG 收尾）→ LA_S
    if re.search(r'\)\s*,\s*$', context_before.rstrip()):
        return 'LA_S'
    # 单词 / 短语作为建块
    return 'LA_W'

# ─── 3. 替换单个源文件 ────────────────────────────────────────────────────────

_MSG_CALL_RE = re.compile(r'MSG\((MSG_\w+)\)')

def transform_file(src_path: str, msg_map: dict[str, str], dry_run: bool):
    with open(src_path, encoding="utf-8") as f:
        original = f.read()

    result = []
    pos = 0
    replaced = 0

    for m in _MSG_CALL_RE.finditer(original):
        key = m.group(1)
        start, end = m.start(), m.end()

        if key not in msg_map:
            print(f"  WARN: {key} not in msg_map — skipping", file=sys.stderr)
            result.append(original[pos:end])
            pos = end
            continue

        en_str = msg_map[key]
        context_before = original[max(0, start-40):start]
        macro = choose_macro(en_str, context_before)

        result.append(original[pos:start])
        # i18n.sh 要求两参数格式: LA_W("str", 0)
        # 第二参数 0 是占位符，i18n.sh 运行后会回写为真正的 ID
        result.append(f'{macro}("{en_str}", 0)')
        pos = end
        replaced += 1

    result.append(original[pos:])
    transformed = ''.join(result)

    # 替换 #include "p2p_lang.h"
    if '#include "p2p_lang.h"' in transformed:
        transformed = transformed.replace('#include "p2p_lang.h"', '#include "LANG.h"')

    if dry_run:
        if replaced:
            print(f"  [DRY] {os.path.basename(src_path)}: {replaced} replacements")
    else:
        if transformed != original:
            with open(src_path, 'w', encoding="utf-8") as f:
                f.write(transformed)
            print(f"  OK   {os.path.basename(src_path)}: {replaced} replacements")
        else:
            print(f"  --   {os.path.basename(src_path)}: no changes")

    return replaced

# ─── 4. 入口 ─────────────────────────────────────────────────────────────────

def main():
    dry_run = '--dry-run' in sys.argv
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    lang_c = os.path.join(repo, 'src', 'p2p_lang.c')
    print(f"Parsing {lang_c} ...")
    msg_map = parse_en_table(lang_c)
    print(f"  Found {len(msg_map)} MSG entries")

    src_files = [
        'src/p2p.c',
        'src/p2p_ice.c',
        'src/p2p_nat.c',
        'src/p2p_route.c',
        'src/p2p_signal_compact.c',
        'src/p2p_signal_pubsub.c',
        'src/p2p_signal_relay.c',
        'src/p2p_stun.c',
        'src/p2p_tcp_punch.c',
        'src/p2p_trans_mbedtls.c',
        'src/p2p_trans_openssl.c',
        'src/p2p_trans_pseudotcp.c',
        'src/p2p_trans_reliable.c',
        'src/p2p_trans_sctp.c',
        'src/p2p_turn.c',
    ]

    total = 0
    print(f"\nTransforming {len(src_files)} files {'(dry-run)' if dry_run else ''}...")
    for rel in src_files:
        path = os.path.join(repo, rel)
        total += transform_file(path, msg_map, dry_run)

    print(f"\nTotal replacements: {total}")

if __name__ == '__main__':
    main()
