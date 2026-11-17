/* power.h —— 电源方案的枚举、读取当前、切换、校验 */
#ifndef PT_POWER_H
#define PT_POWER_H

#include "ptdef.h"
#include <windows.h>

#define MAX_SCHEMES   64
#define SCHEME_NAME_MAX 128

struct SchemeInfo {
    GUID    guid;
    WCHAR   name[SCHEME_NAME_MAX];
};

struct SchemeList {
    SchemeInfo items[MAX_SCHEMES];
    int        count;
};

/* 枚举系统全部电源方案，返回 FALSE 表示一个都拿不到 */
BOOL PowerEnumSchemes(SchemeList* out);

/* 读取当前激活方案 GUID */
BOOL PowerGetActiveGuid(GUID* out);

/* 切换激活方案，返回 ERROR_SUCCESS 表示成功，否则为 Win32 错误码 */
DWORD PowerSetActiveGuid(const GUID* g);

/* 在列表中查找；找到返回 TRUE 并填充 out（可为 NULL） */
BOOL PowerFindByGuid(const SchemeList* list, const GUID* g, SchemeInfo* out);

/* GUID 比较 / 解析 / 格式化 */
BOOL GuidEqual(const GUID* a, const GUID* b);
BOOL GuidParse(const WCHAR* text, GUID* out);
void GuidFormat(const GUID* g, WCHAR* out, int cchOut);

#endif /* PT_POWER_H */
