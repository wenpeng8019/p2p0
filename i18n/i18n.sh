#!/bin/bash
#
# 多语言字符串提取工具 - 生成 LANG.h 和 LANG.c
#
# 用法：./i18n/i18n.sh <source_dir>
# 示例：./i18n/i18n.sh p2p_ping
#
# ============================================================================
# 宏系统说明
# ============================================================================
#
# LA_W(str, id) - Words 词汇
#   用于单个词或短语，常用于状态名、按钮文本等
#   示例：LA_W("CONNECTED", SID_W2)
#
# LA_S(str, id) - Strings 字符串
#   用于完整句子或长文本
#   示例：LA_S("Connection established", SID_S0)
#
# LA_F(str, id, ...) - Formats 格式化字符串
#   用于包含 printf 格式符的字符串
#   示例：LA_F("State: %s (%d)", SID_F0, state, code)
#
# ============================================================================
# ID 命名规则（按字母顺序排列）
# ============================================================================
#
# SID_PREDEFINED - 预定义基础 ID（默认 -1，可通过编译选项重定义）
# SID_PRED       - 枚举基础 ID（= SID_PREDEFINED），后续 ID 从此递增
#
# SID_W0~W8   - Words 词汇 ID
# SID_F0~F3   - Formats 格式化字符串 ID
# SID_S0~S26  - Strings 普通字符串 ID
#
# 各类型独立编号，增强代码可读性
#
# 说明：通过设置 SID_PREDEFINED 可以调整所有 ID 的起始值
#       例如：-DSID_PREDEFINED=100 可让所有 ID 从 100 开始
#
# ============================================================================
# 归并规则（去重机制）
# ============================================================================
#
# 不同类型的字符串有不同的归并规则：
#
# 【LA_W - Words 词汇】
#
# 1. Trim 处理：去除字符串首尾空格
#    " CLOSING"   -> "CLOSING"
#    "ERROR "     -> "ERROR"
#    " UNKNOWN "  -> "UNKNOWN"
#
# 2. 忽略大小写：转为小写后作为 key 比对
#    "CLosED"     -> 小写 "closed"  -> 归并到 "CLOSED"
#    "CLOsiNG"    -> 小写 "closing" -> 归并到 "CLOSING"
#    "PUNchiNG"   -> 小写 "punching"-> 归并到 "PUNCHING"
#
# 3. 排序和去重：基于 key（小写版本）进行字母顺序排序和去重
#    排序命令：sort -u -t'|' -k2,2
#    - 相同 key 的多个变体只保留第一个
#    - 按 key 字母序排列：closed < closing < connected < error < init...
#    - ID 编号：SID_W0=CLOSED, SID_W1=CLOSING, SID_W2=CONNECTED...
#
# 4. 源码保留原样：源文件中保留原始字符串（包括空格和大小写）
#    代码：LA_W(" CLOSING", SID_W1)   // 保留前导空格
#    映射：" CLOSING" -> trim+lc -> "closing" -> SID_W1
#    输出：LANG.c 中统一存储规范化版本 "CLOSING"
#
# 归并示例：
#   LA_W("CLOSING", SID_W1)      // 原始标准版本
#   LA_W(" CLOSING", SID_W1)     // 前导空格 -> 归并
#   LA_W("CLOsiNG", SID_W1)      // 大小写混合 -> 归并
#   LA_W("closing ", SID_W1)     // 小写+尾随空格 -> 归并
#
# 所有变体都指向同一个 SID_W1，LANG.c 中只存储一份 "CLOSING"
# （SID_W1 是因为 "closing" 在字母表中排序到第2位，从0开始编号）
#
# 【LA_S - Strings 字符串】
#
# 1. 只忽略大小写：转为小写后作为 key 比对
#    "Connection Error" 和 "connection error" -> 归并
#
# 2. 空格格式必须一致：不做 trim 处理
#    " Connection Error" 和 "Connection Error" -> 不归并（前导空格不同）
#    "Connection Error " 和 "Connection Error" -> 不归并（尾随空格不同）
#
# 【LA_F - Formats 格式化字符串】
#
# 1. 完全一致：不做任何转换
#    格式化字符串必须完全匹配才能归并（包括大小写和空格）
#
# ============================================================================
# 工作流程
# ============================================================================
#
# 1. 扫描源文件，提取所有 LA_W/LA_S/LA_F 宏中的字符串
# 2. 对每个字符串生成归并 key（trim + lowercase）
# 3. 按 key 去重（sort -u -t'|' -k2,2），保留首次出现的原始字符串
# 4. 按 key 字母顺序排序，生成连续的 SID_W/F/S 编号
#    - Words 按小写字母序：closed, closing, connected, error, init...
#    - Formats 按小写字母序
#    - Strings 按小写字母序
# 5. 生成 LANG.h（枚举定义）和 LANG.c（字符串数组）
# 6. 回写源文件，更新所有宏的第二个参数为正确的 SID
#
# 注意：ID 编号由排序决定，添加/删除字符串可能导致 ID 重新分配
#
# ============================================================================

set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: $0 <source_dir> [--export]"
    echo "Example: $0 p2p_ping"
    echo "Options:"
    echo "  --export    Export lang.en template file for translations"
    exit 1
fi

SOURCE_DIR="$1"
EXPORT_LANG_EN=0

# 解析选项
shift
while [ $# -gt 0 ]; do
    case "$1" in
        --export)
            EXPORT_LANG_EN=1
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

OUTPUT_H="$SOURCE_DIR/.LANG.h"
OUTPUT_C="$SOURCE_DIR/.LANG.c"
USER_LANG_H="$SOURCE_DIR/LANG.h"

if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: Directory not found: $SOURCE_DIR"
    exit 1
fi

# 检查是否存在用户定义的 LANG.h，如果不存在则生成模板
if [ ! -f "$USER_LANG_H" ]; then
    echo "Creating template LANG.h in $SOURCE_DIR..."
    cat > "$USER_LANG_H" <<'EOF'
#ifndef LANG_H_
#define LANG_H_

#include <i18n.h>

typedef enum {
    /* 预定义字符串 ID（在此添加项目特定的预定义字符串）*/
    /* 示例：
    SID_CUSTOM0,
    SID_CUSTOM1,
    */
    
    PRED_NUM,
};

/* 预定义字符串内容（对应上面的枚举）*/
/* 示例：
#define STR_CUSTOM0 "Custom String 0"
#define STR_CUSTOM1 "Custom String 1"
*/

/* 设置预定义基础 ID（自动生成的 ID 从此值+1 开始）*/
#define SID_PREDEFINED (PRED_NUM - 1)

/* 包含自动生成的语言 ID 定义（必须在 SID_PREDEFINED 之后）*/
#include ".LANG.h"

/* 语言初始化函数（自动生成，请勿修改）*/
static inline void lang_init(void) {
    lang_def(lang_en, sizeof(lang_en) / sizeof(lang_en[0]), SID_F);
}

#endif /* LANG_H_ */
EOF
    echo "  Created: $USER_LANG_H"
fi

echo "=== Language String Extractor ==="
echo "Source: $SOURCE_DIR"
echo

# 临时文件
TEMP_ALL=$(mktemp)
TEMP_WORDS=$(mktemp)
TEMP_FORMATS=$(mktemp)
TEMP_STRINGS=$(mktemp)
TEMP_MAP=$(mktemp)

cleanup() {
    rm -f "$TEMP_ALL" "$TEMP_WORDS" "$TEMP_FORMATS" "$TEMP_STRINGS" "$TEMP_MAP"
}
trap cleanup EXIT

# 提取所有 LA_W/LA_S/LA_F
find "$SOURCE_DIR" -name "*.c" -o -name "*.h" | while read -r file; do
    perl -ne '
        # 提取 LA_W
        while (/LA_W\s*\(\s*"((?:[^"\\]|\\.)*)"/g) {
            my $s = $1;
            $s =~ s/^\s+|\s+$//g;  # trim
            my $key = lc($s);
            print "W|$key|$s\n";
        }
        # 提取 LA_S  
        while (/LA_S\s*\(\s*"((?:[^"\\]|\\.)*)"/g) {
            my $s = $1;
            my $key = lc($s);
            print "S|$key|$s\n";
        }
        # 提取 LA_F (完全一致，不做转换)
        while (/LA_F\s*\(\s*"((?:[^"\\]|\\.)*)"/g) {
            my $s = $1;
            print "F|$s|$s\n";
        }
    ' "$file"
done > "$TEMP_ALL"

# 分类并去重
grep "^W|" "$TEMP_ALL" | sort -u -t'|' -k2,2 > "$TEMP_WORDS" || true
grep "^F|" "$TEMP_ALL" | sort -u -t'|' -k2,2 > "$TEMP_FORMATS" || true
grep "^S|" "$TEMP_ALL" | sort -u -t'|' -k2,2 > "$TEMP_STRINGS" || true

# 统计
word_count=$(wc -l < "$TEMP_WORDS" | tr -d ' ')
format_count=$(wc -l < "$TEMP_FORMATS" | tr -d ' ')
string_count=$(wc -l < "$TEMP_STRINGS" | tr -d ' ')
total=$((word_count + format_count + string_count))

echo "Words (LA_W):   $word_count"
echo "Formats (LA_F): $format_count"
echo "Strings (LA_S): $string_count"
echo "Total:          $total"
echo

if [ "$total" -eq 0 ]; then
    echo "Warning: No LA_W/LA_S/LA_F macros found"
    exit 0
fi

# 生成 .h 文件
cat > "$OUTPUT_H" <<EOF
/*
 * Auto-generated language IDs
 * Generated: $(date '+%Y-%m-%d %H:%M:%S')
 * 
 * DO NOT EDIT - Regenerate with: ./i18n/i18n.sh
 */

#ifndef LANG_H__
#define LANG_H__

#ifndef SID_PREDEFINED
#   define SID_PREDEFINED -1
#endif

enum {
    SID_PRED = SID_PREDEFINED,  /* 基础 ID，后续 ID 从此开始递增 */
    
EOF

sid=0

# 词
if [ "$word_count" -gt 0 ]; then
    echo "    /* Words (LA_W) */" >> "$OUTPUT_H"
    wid=0
    while IFS='|' read -r type key str; do
        echo "    SID_W${wid},  /* \"$str\" */" >> "$OUTPUT_H"
        echo "W|$key|SID_W${wid}" >> "$TEMP_MAP"
        wid=$((wid + 1))
        sid=$((sid + 1))
    done < "$TEMP_WORDS"
    echo "" >> "$OUTPUT_H"
fi

# 字符串
if [ "$string_count" -gt 0 ]; then
    echo "    /* Strings (LA_S) */" >> "$OUTPUT_H"
    strid=0
    while IFS='|' read -r type key str; do
        echo "    SID_S${strid},  /* \"$str\" */" >> "$OUTPUT_H"
        echo "S|$key|SID_S${strid}" >> "$TEMP_MAP"
        strid=$((strid + 1))
        sid=$((sid + 1))
    done < "$TEMP_STRINGS"
    echo "" >> "$OUTPUT_H"
fi

# 格式化（放在最后，方便校验）
if [ "$format_count" -gt 0 ]; then
    echo "    /* Formats (LA_F) - Format strings for validation */" >> "$OUTPUT_H"
    fid=0
    while IFS='|' read -r type key str; do
        params=$(echo "$str" | grep -o '%[sdifuxXc]' | tr '\n' ',' | sed 's/,$//')
        if [ -n "$params" ]; then
            echo "    SID_F${fid},  /* \"$str\" ($params) */" >> "$OUTPUT_H"
        else
            echo "    SID_F${fid},  /* \"$str\" */" >> "$OUTPUT_H"
        fi
        echo "F|$key|SID_F${fid}" >> "$TEMP_MAP"
        fid=$((fid + 1))
        sid=$((sid + 1))
    done < "$TEMP_FORMATS"
    echo "" >> "$OUTPUT_H"
fi

cat >> "$OUTPUT_H" <<EOF
    SID_NUM = $sid
};

EOF

# 添加 SID_F 定义（格式字符串起始位置标记）
if [ "$format_count" -gt 0 ]; then
    cat >> "$OUTPUT_H" <<'EOF'
/* 格式字符串起始位置（用于验证） */
#define SID_F SID_F0

EOF
else
    cat >> "$OUTPUT_H" <<'EOF'
/* 无格式字符串 */
#define SID_F SID_NUM

EOF
fi

cat >> "$OUTPUT_H" <<'EOF'
/* 字符串表 */
extern const char* lang_en[SID_NUM];

#endif /* LANG_H__ */
EOF

# 生成 .c 文件
cat > "$OUTPUT_C" <<EOF
/*
 * Auto-generated language strings
 * Generated: $(date '+%Y-%m-%d %H:%M:%S')
 */

#include ".LANG.h"

/* 字符串表 */
const char* lang_en[SID_NUM] = {
EOF

# 提取 LANG.h 中的预定义项（如果存在）
if [ -f "$USER_LANG_H" ]; then
    # 提取枚举中 PRED_NUM 之前的项和对应的 STR_ 宏
    perl -ne '
        BEGIN { $in_enum = 0; $in_comment = 0; }
        # 多行注释处理
        if (/\/\*/) { $in_comment = 1; }
        if (/\*\//) { $in_comment = 0; next; }
        next if $in_comment;
        # 单行注释跳过
        next if /^\s*\/\//;
        
        if (/typedef\s+enum\s*\{/) { $in_enum = 1; next; }
        if ($in_enum && /PRED_NUM/) { $in_enum = 0; }
        if ($in_enum && /^\s*(SID_\w+)\s*,?\s*$/) {
            push @sids, $1;
        }
        if (!$in_enum && /^\s*#\s*define\s+(STR_\w+)\s+(.*)$/) {
            $strs{$1} = $2;
        }
        END {
            for my $sid (@sids) {
                my $str_name = $sid;
                $str_name =~ s/^SID_/STR_/;
                if (exists $strs{$str_name}) {
                    print "    /* [$sid] = $str_name */\n";
                }
            }
        }
    ' "$USER_LANG_H" >> "$OUTPUT_C"
fi

# 词
if [ "$word_count" -gt 0 ]; then
    wid=0
    while IFS='|' read -r type key str; do
        # 转义字符串
        escaped=$(echo "$str" | sed 's/\\/\\\\/g; s/"/\\"/g')
        echo "    [SID_W${wid}] = \"$escaped\"," >> "$OUTPUT_C"
        wid=$((wid + 1))
    done < "$TEMP_WORDS"
fi

# 字符串
if [ "$string_count" -gt 0 ]; then
    strid=0
    while IFS='|' read -r type key str; do
        escaped=$(echo "$str" | sed 's/\\/\\\\/g; s/"/\\"/g')
        echo "    [SID_S${strid}] = \"$escaped\"," >> "$OUTPUT_C"
        strid=$((strid + 1))
    done < "$TEMP_STRINGS"
fi

# 格式化
if [ "$format_count" -gt 0 ]; then
    fid=0
    while IFS='|' read -r type key str; do
        escaped=$(echo "$str" | sed 's/\\/\\\\/g; s/"/\\"/g')
        echo "    [SID_F${fid}] = \"$escaped\"," >> "$OUTPUT_C"
        fid=$((fid + 1))
    done < "$TEMP_FORMATS"
fi

cat >> "$OUTPUT_C" <<EOF
};
EOF

# 生成 lang.en 文本文件（仅在指定 --export 选项时）
if [ "$EXPORT_LANG_EN" -eq 1 ]; then
    OUTPUT_LANG_EN="$SOURCE_DIR/lang.en"
    cat > "$OUTPUT_LANG_EN" <<'EOF'
# Language Table (one string per line)
# Use this file as a template for other language translations
# Line number corresponds to string ID (starting from 0)
# Lines starting with '#' are comments
# Note: No blank lines allowed between comments and the string table
EOF

    # 如果有预定义项，先提取其值
    if [ -f "$USER_LANG_H" ]; then
        perl -ne '
            BEGIN { $in_enum = 0; $in_comment = 0; }
            # 多行注释处理
            if (/\/\*/) { $in_comment = 1; }
            if (/\*\//) { $in_comment = 0; next; }
            next if $in_comment;
            # 单行注释跳过
            next if /^\s*\/\//;
            
            if (/typedef\s+enum\s*\{/) { $in_enum = 1; next; }
            if ($in_enum && /PRED_NUM/) { $in_enum = 0; }
            if ($in_enum && /^\s*(SID_\w+)\s*,?\s*$/) {
                push @sids, $1;
            }
            if (!$in_enum && /^\s*#\s*define\s+(STR_\w+)\s+"((?:[^"\\]|\\.)*)"/) {
                $strs{$1} = $2;
            }
            END {
                for my $sid (@sids) {
                    my $str_name = $sid;
                    $str_name =~ s/^SID_/STR_/;
                    if (exists $strs{$str_name}) {
                        my $val = $strs{$str_name};
                        # 反转义
                        $val =~ s/\\n/\n/g;
                        $val =~ s/\\t/\t/g;
                        $val =~ s/\\"/"/g;
                        $val =~ s/\\\\/\\/g;
                        print "$val\n";
                    }
                }
            }
        ' "$USER_LANG_H" >> "$OUTPUT_LANG_EN"
    fi

    # 输出自动提取的字符串（按 W, S, F 顺序）
    if [ "$word_count" -gt 0 ]; then
        while IFS='|' read -r type key str; do
            echo "$str" >> "$OUTPUT_LANG_EN"
        done < "$TEMP_WORDS"
    fi

    if [ "$string_count" -gt 0 ]; then
        while IFS='|' read -r type key str; do
            echo "$str" >> "$OUTPUT_LANG_EN"
        done < "$TEMP_STRINGS"
    fi

    if [ "$format_count" -gt 0 ]; then
        while IFS='|' read -r type key str; do
            echo "$str" >> "$OUTPUT_LANG_EN"
        done < "$TEMP_FORMATS"
    fi
fi

echo "Generated:"
echo "  $OUTPUT_H ($sid IDs)"
echo "  $OUTPUT_C"
if [ "$EXPORT_LANG_EN" -eq 1 ]; then
    echo "  $OUTPUT_LANG_EN"
fi
echo

# 回写源文件，替换 ID
echo "Updating source files..."
find "$SOURCE_DIR" -name "*.c" -o -name "*.h" | while read -r file; do
    # 跳过生成的文件
    if [ "$file" = "$OUTPUT_H" ] || [ "$file" = "$OUTPUT_C" ]; then
        continue
    fi
    
    # 使用 Perl 原地替换
    perl -i -pe '
        BEGIN {
            # 加载映射表 (格式: W|key|SID)
            open(my $fh, "<", "'"$TEMP_MAP"'") or die;
            while (my $line = <$fh>) {
                chomp $line;
                my ($type, $key, $sid) = split(/\|/, $line, 3);
                $map{$type}{$key} = $sid;
            }
            close($fh);
        }
        
        # 替换 LA_W
        s{(LA_W\s*\(\s*"((?:[^"\\\\]|\\\\.)*?)"\s*,\s*)(?:0|SID_[WFS]\d+)}{
            my $prefix = $1;
            my $str = $2;
            # 生成 key: trim + lowercase
            my $key = $str;
            $key =~ s/^\s+|\s+$//g;
            $key = lc($key);
            my $sid = $map{"W"}{$key} || "0";
            $prefix . $sid;
        }ge;
        
        # 替换 LA_S
        s{(LA_S\s*\(\s*"((?:[^"\\\\]|\\\\.)*?)"\s*,\s*)(?:0|SID_[WFS]\d+)}{
            my $prefix = $1;
            my $str = $2;
            my $key = $str;
            $key =~ s/^\s+|\s+$//g;
            $key = lc($key);
            my $sid = $map{"S"}{$key} || "0";
            $prefix . $sid;
        }ge;
        
        # 替换 LA_F (完全匹配)
        s{(LA_F\s*\(\s*"((?:[^"\\\\]|\\\\.)*?)"\s*,\s*)(?:0|SID_[WFS]\d+)}{
            my $prefix = $1;
            my $str = $2;
            my $sid = $map{"F"}{$str} || "0";
            $prefix . $sid;
        }ge;
    ' "$file" && echo "  Updated: $file"
done

echo
echo "Done! Source files updated with correct SID_W/F/Sxxx IDs"
echo "Next: Rebuild with updated LANG.c"
