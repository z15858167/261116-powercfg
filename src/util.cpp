/* util.cpp */
#include "util.h"
#include <stdarg.h>
#include <shlobj.h>

#define LOG_MAX_BYTES (64 * 1024)

static int    g_logLevel = 0;
static WCHAR  g_logPath[MAX_PATH] = { 0 };
static WCHAR  g_idleLogPath[MAX_PATH] = { 0 };

/* =====================  字符串构建器  ===================== */

void SbInit(Sb* sb, WCHAR* buf, int cch)
{
    sb->buf = buf;
    sb->cch = cch;
    sb->len = 0;
    if (cch > 0) buf[0] = 0;
}

void SbClear(Sb* sb)
{
    sb->len = 0;
    if (sb->cch > 0) sb->buf[0] = 0;
}

void SbAppend(Sb* sb, const WCHAR* s)
{
    if (!s || sb->len >= sb->cch - 1) return;
    lstrcpynW(sb->buf + sb->len, s, sb->cch - sb->len);
    sb->len = lstrlenW(sb->buf);
}

void SbAppendF(Sb* sb, const WCHAR* fmt, ...)
{
    va_list ap;
    if (sb->len >= sb->cch - 1) return;
    /* wvsprintfW 内部限制 1024 字符，预留足够空间避免越界 */
    if (sb->cch - sb->len < 1024) return;
    va_start(ap, fmt);
    wvsprintfW(sb->buf + sb->len, fmt, ap);
    va_end(ap);
    sb->len = lstrlenW(sb->buf);
}

/* =====================  字符串  ===================== */

void StrCopy(WCHAR* dst, int cchDst, const WCHAR* src)
{
    if (!dst || cchDst <= 0) return;
    if (!src) { dst[0] = 0; return; }
    lstrcpynW(dst, src, cchDst);
}

BOOL StrEqualI(const WCHAR* a, const WCHAR* b)
{
    if (a == b) return TRUE;
    if (!a || !b) return FALSE;
    return lstrcmpiW(a, b) == 0;
}

BOOL StrIsEmpty(const WCHAR* s)
{
    return (!s || s[0] == 0) ? TRUE : FALSE;
}

void StrTrim(WCHAR* s)
{
    WCHAR* p;
    int len;
    if (!s) return;
    p = s;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') p++;
    if (p != s) lstrcpyW(s, p);
    len = lstrlenW(s);
    while (len > 0) {
        WCHAR c = s[len - 1];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') s[--len] = 0;
        else break;
    }
}

void StrFirstChars(const WCHAR* src, WCHAR* dst, int cchDst, int n)
{
    int i = 0;
    if (!dst || cchDst <= 0) return;
    dst[0] = 0;
    if (!src) return;
    while (src[i] && i < n && i < cchDst - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* =====================  路径与文件  ===================== */

void PathGetExeDir(WCHAR* out, int cchOut)
{
    WCHAR path[MAX_PATH];
    WCHAR* p;
    out[0] = 0;
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) return;
    p = path + lstrlenW(path);
    while (p > path && *p != L'\\') p--;
    if (p > path) *p = 0;
    StrCopy(out, cchOut, path);
}

void PathGetExePath(WCHAR* out, int cchOut)
{
    WCHAR path[MAX_PATH];
    out[0] = 0;
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) return;
    StrCopy(out, cchOut, path);
}

BOOL PathCombine(WCHAR* out, int cchOut, const WCHAR* dir, const WCHAR* name)
{
    int dl;
    if (!out || cchOut <= 0) return FALSE;
    out[0] = 0;
    if (!dir || !name) return FALSE;
    StrCopy(out, cchOut, dir);
    dl = lstrlenW(out);
    if (dl > 0 && out[dl - 1] != L'\\' && dl < cchOut - 1) {
        out[dl] = L'\\';
        out[dl + 1] = 0;
    }
    if (lstrlenW(out) + lstrlenW(name) >= cchOut) return FALSE;
    lstrcatW(out, name);
    return TRUE;
}

BOOL FileExists(const WCHAR* path)
{
    DWORD attr;
    if (!path) return FALSE;
    attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return FALSE;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? FALSE : TRUE;
}

BOOL DirEnsure(const WCHAR* path)
{
    if (!path || !path[0]) return FALSE;
    if (SHCreateDirectoryExW(NULL, path, NULL) == ERROR_SUCCESS) return TRUE;
    /* 已存在也视为成功 */
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

BOOL DirIsWritable(const WCHAR* dir)
{
    WCHAR probe[MAX_PATH];
    HANDLE h;
    if (!PathCombine(probe, MAX_PATH, dir, L"PowerToggle.wtest")) return FALSE;
    h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(h);
    return TRUE;
}

void StrFormatLabel(WCHAR* out, int cchOut, const WCHAR* base, const WCHAR* detail)
{
    Sb sb;

    if (!out || cchOut <= 0) return;
    out[0] = 0;
    SbInit(&sb, out, cchOut);
    if (base) SbAppend(&sb, base);
    if (detail && detail[0]) {
        SbAppend(&sb, L"（");
        SbAppend(&sb, detail);
        SbAppend(&sb, L"）");
    }
}

/* =====================  日志  ===================== */

void LogSetLevel(int level)
{
    g_logLevel = level;
}

void LogSetPath(const WCHAR* path)
{
    if (path) StrCopy(g_logPath, MAX_PATH, path);
    else g_logPath[0] = 0;
}

/* 追加 UTF-8 字节；allowRotate 为真且文件超过上限时清空（仅通用日志使用）。
   空闲切换专用日志传 FALSE，保证历史记录只增不减。 */
static void AppendUtf8(const WCHAR* path, const char* bytes, int len, BOOL allowRotate)
{
    HANDLE h;
    DWORD written = 0;
    LARGE_INTEGER size;
    if (len <= 0 || !path[0]) return;
    h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (allowRotate) {
        size.QuadPart = 0;
        if (GetFileSizeEx(h, &size) && size.QuadPart > LOG_MAX_BYTES) {
            CloseHandle(h);
            h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) return;
        }
    }
    WriteFile(h, bytes, (DWORD)len, &written, NULL);
    CloseHandle(h);
}

/* 统一格式化：level < 0 时不输出 [level] 前缀（专用日志更简洁）。 */
static void LogEmit(const WCHAR* path, int level, BOOL allowRotate,
                    const WCHAR* fmt, va_list ap)
{
    WCHAR msg[1024];
    WCHAR line[1200];
    char  utf8[2400];
    SYSTEMTIME st;
    int n;

    wvsprintfW(msg, fmt, ap);
    GetLocalTime(&st);
    if (level >= 0) {
        wsprintfW(line, L"%04d-%02d-%02d %02d:%02d:%02d [%d] %s\r\n",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                  level, msg);
    } else {
        wsprintfW(line, L"%04d-%02d-%02d %02d:%02d:%02d %s\r\n",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
    }

    n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), NULL, NULL);
    if (n > 1) AppendUtf8(path, utf8, n - 1, allowRotate);
}

void LogFmt(int level, const WCHAR* fmt, ...)
{
    va_list ap;
    if (level <= 0 || g_logLevel < level || !g_logPath[0]) return;
    va_start(ap, fmt);
    LogEmit(g_logPath, level, TRUE, fmt, ap);
    va_end(ap);
}

void LogSetIdlePath(const WCHAR* path)
{
    if (path) StrCopy(g_idleLogPath, MAX_PATH, path);
    else g_idleLogPath[0] = 0;
}

void IdleLog(const WCHAR* fmt, ...)
{
    va_list ap;
    if (!g_idleLogPath[0]) return;
    va_start(ap, fmt);
    LogEmit(g_idleLogPath, -1, FALSE, fmt, ap);
    va_end(ap);
}

/* =====================  错误码  ===================== */

void ErrText(DWORD code, WCHAR* out, int cchOut)
{
    WCHAR* sys = NULL;
    if (!out || cchOut <= 0) return;
    out[0] = 0;
    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, code, 0, (LPWSTR)&sys, 0, NULL) && sys) {
        StrCopy(out, cchOut, sys);
        StrTrim(out);
        LocalFree(sys);
        return;
    }
    wsprintfW(out, L"错误码 %u", (unsigned)code);
}
