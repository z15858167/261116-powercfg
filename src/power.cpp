/* power.cpp */
#include "power.h"
#include "util.h"
#include <powrprof.h>
#include <objbase.h>

BOOL PowerEnumSchemes(SchemeList* out)
{
    DWORD idx;

    if (!out) return FALSE;
    out->count = 0;

    for (idx = 0; idx < (DWORD)MAX_SCHEMES; idx++) {
        GUID g;
        DWORD szGuid = (DWORD)sizeof(g);
        DWORD r = PowerEnumerate(NULL, NULL, NULL, ACCESS_SCHEME, idx,
                                 (PBYTE)&g, &szGuid);
        if (r != ERROR_SUCCESS) break;   /* ERROR_NO_MORE_ITEMS 或其它错误 */

        {
            SchemeInfo* si = &out->items[out->count];
            BYTE  raw[512];
            DWORD sz = sizeof(raw);

            si->guid = g;
            si->name[0] = 0;

            ZeroMemory(raw, sizeof(raw));
            if (PowerReadFriendlyName(NULL, &g, NULL, NULL, raw, &sz) == ERROR_SUCCESS) {
                StrCopy(si->name, SCHEME_NAME_MAX, (const WCHAR*)raw);
            }
            if (si->name[0] == 0) {
                GuidFormat(&g, si->name, SCHEME_NAME_MAX);
            }
        }
        out->count++;
    }

    LogFmt(2, L"枚举到 %d 个电源方案", out->count);
    return out->count > 0 ? TRUE : FALSE;
}

BOOL PowerGetActiveGuid(GUID* out)
{
    GUID* p = NULL;
    DWORD r;
    if (!out) return FALSE;
    r = PowerGetActiveScheme(NULL, &p);
    if (r != ERROR_SUCCESS || !p) {
        LogFmt(1, L"PowerGetActiveScheme 失败: %u", (unsigned)r);
        return FALSE;
    }
    *out = *p;
    LocalFree(p);
    return TRUE;
}

DWORD PowerSetActiveGuid(const GUID* g)
{
    DWORD r;
    if (!g) return ERROR_INVALID_PARAMETER;
    r = PowerSetActiveScheme(NULL, g);
    if (r != ERROR_SUCCESS) {
        LogFmt(1, L"PowerSetActiveScheme 失败: %u", (unsigned)r);
    }
    return r;
}

BOOL PowerFindByGuid(const SchemeList* list, const GUID* g, SchemeInfo* out)
{
    int i;
    if (!list || !g) return FALSE;
    for (i = 0; i < list->count; i++) {
        if (GuidEqual(&list->items[i].guid, g)) {
            if (out) *out = list->items[i];
            return TRUE;
        }
    }
    return FALSE;
}

BOOL GuidEqual(const GUID* a, const GUID* b)
{
    if (!a || !b) return FALSE;
    return (memcmp(a, b, sizeof(GUID)) == 0) ? TRUE : FALSE;
}

BOOL GuidParse(const WCHAR* text, GUID* out)
{
    GUID g;
    if (!text || !out) return FALSE;
    if (lstrlenW(text) != 38 || text[0] != L'{' || text[37] != L'}') return FALSE;
    if (FAILED(CLSIDFromString(text, &g))) return FALSE;
    *out = g;
    return TRUE;
}

void GuidFormat(const GUID* g, WCHAR* out, int cchOut)
{
    if (!out || cchOut <= 0) return;
    out[0] = 0;
    if (!g) return;
    if (StringFromGUID2(*g, out, cchOut) <= 0) out[0] = 0;
}
