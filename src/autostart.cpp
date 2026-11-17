/* autostart.cpp */
#include "autostart.h"
#include "util.h"

#define RUN_KEY_PATH  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE     L"PowerToggle"

static BOOL ReadRunValue(WCHAR* out, int cchOut)
{
    HKEY  key = NULL;
    DWORD type = 0;
    DWORD size = 0;
    LONG  r;

    if (out && cchOut > 0) out[0] = 0;

    r = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_QUERY_VALUE, &key);
    if (r != ERROR_SUCCESS) return FALSE;

    r = RegQueryValueExW(key, RUN_VALUE, NULL, &type, NULL, &size);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        size == 0 || size > (DWORD)(cchOut * sizeof(WCHAR))) {
        RegCloseKey(key);
        return FALSE;
    }

    r = RegQueryValueExW(key, RUN_VALUE, NULL, &type, (LPBYTE)out, &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) { out[0] = 0; return FALSE; }
    out[cchOut - 1] = 0;
    return TRUE;
}

BOOL AutoStartIsOn(void)
{
    WCHAR buf[MAX_PATH * 2];
    return ReadRunValue(buf, MAX_PATH * 2);
}

BOOL AutoStartApply(BOOL on, const WCHAR* exePath)
{
    HKEY key = NULL;
    LONG r;
    BOOL ok = FALSE;

    if (!on) {
        r = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_SET_VALUE, &key);
        if (r != ERROR_SUCCESS) return TRUE;   /* 键都不存在，视为已关闭 */
        RegDeleteValueW(key, RUN_VALUE);
        RegCloseKey(key);
        return TRUE;
    }

    if (!exePath || !exePath[0]) return FALSE;

    r = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS) return FALSE;

    {
        WCHAR quoted[MAX_PATH * 2 + 8];
        wsprintfW(quoted, L"\"%s\"", exePath);
        r = RegSetValueExW(key, RUN_VALUE, 0, REG_SZ, (const BYTE*)quoted,
                           (DWORD)((lstrlenW(quoted) + 1) * sizeof(WCHAR)));
        ok = (r == ERROR_SUCCESS) ? TRUE : FALSE;
    }

    RegCloseKey(key);
    return ok;
}

void AutoStartSync(const WCHAR* exePath)
{
    WCHAR cur[MAX_PATH * 2];
    WCHAR want[MAX_PATH * 2 + 8];

    if (!exePath || !exePath[0]) return;
    if (!ReadRunValue(cur, MAX_PATH * 2)) return;   /* 未开启自启，不处理 */

    wsprintfW(want, L"\"%s\"", exePath);
    if (!StrEqualI(cur, want)) {
        AutoStartApply(TRUE, exePath);
        LogFmt(2, L"自启路径已更新: %s", want);
    }
}
