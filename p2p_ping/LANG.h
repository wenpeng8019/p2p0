#ifndef LANG_H_
#define LANG_H_

#include <i18n.h>

typedef enum {
    /* 预定义字符串 ID（在此添加项目特定的预定义字符串）*/
    /* 示例：
    LA_CUSTOM0,
    LA_CUSTOM1,
    */
    
    PRED_NUM,
};

/* 预定义字符串内容（对应上面的枚举）*/
/* 示例：
#define STR_CUSTOM0 "Custom String 0"
#define STR_CUSTOM1 "Custom String 1"
*/

/* 设置预定义基础 ID（自动生成的 ID 从此值+1 开始）*/
#define LA_PREDEFINED (PRED_NUM - 1)

/* 包含自动生成的语言 ID 定义（必须在 LA_PREDEFINED 之后）*/
#include ".LANG.h"

/* 语言初始化函数（自动生成，请勿修改）*/
static inline void lang_init(void) {
    lang_def(lang_en, sizeof(lang_en) / sizeof(lang_en[0]), LA_FMT_START);
}

#endif /* LANG_H_ */
