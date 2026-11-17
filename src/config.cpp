/* config.cpp */
#include "config.h"
#include "util.h"
#include "power.h"
#include <stdlib.h>
#include <shlobj.h>

#define CONFIG_BUF_SIZE (16 * 1024)
#define CONFIG_FILE_MAX (1024 * 1024)

static WCHAR g_saveBuf[CONFIG_BUF_SIZE];

/* =====================  颜色  ===================== */

static int HexVal(WCHAR ch)
{
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
    if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
    return -1;
}

COLORREF ColorFromHex(const WCHAR* text, COLORREF def)
{
    int r, g, b, i;
    if (!text) return def;
    if (text[0] == L'#') text++;
    if (lstrlenW(text) < 6) return def;
    for (i = 0; i < 6; i++) if (HexVal(text[i]) < 0) return def;
    r = HexVal(text[0]) * 16 + HexVal(text[1]);
    g = HexVal(text[2]) * 16 + HexVal(text[3]);
    b = HexVal(text[4]) * 16 + HexVal(text[5]);
    return RGB(r, g, b);
}

void ColorToHex(COLORREF c, WCHAR* out, int cchOut)
{
    if (!out || cchOut < 8) return;
    wsprintfW(out, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
}

/* =====================  默认值  ===================== */

void ConfigDefaults(Config* c)
{
    if (!c) return;
    ZeroMemory(c, sizeof(Config));

    c->colorA       = RGB(0x2E, 0x7D, 0x32);
    c->colorB       = RGB(0xC6, 0x28, 0x28);
    c->fgColor      = RGB(0xFF, 0xFF, 0xFF);
    c->neutralColor = RGB(0x6B, 0x72, 0x80);

    c->idleEnabled      = TRUE;
    c->idleThresholdSec = 600;
    c->idleTarget       = TARGET_A;
    c->revertOnResume   = FALSE;
    c->cooldownSec      = 5;

    c->autoStart    = FALSE;
    c->showBalloon  = FALSE;
    c->firstRunDone = FALSE;
    c->logLevel     = 0;
}

/* =====================  读取文件  ===================== */

static BOOL ReadAllText(const WCHAR* path, WCHAR* out, int cchOut)
{
    HANDLE h;
    DWORD size, read = 0;
    BYTE* buf;
    BOOL ok = FALSE;

    if (!out || cchOut <= 0) return FALSE;
    out[0] = 0;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > CONFIG_FILE_MAX) {
        CloseHandle(h);
        return FALSE;
    }

    buf = (BYTE*)malloc(size + 4);
    if (!buf) { CloseHandle(h); return FALSE; }
    ZeroMemory(buf, size + 4);

    if (ReadFile(h, buf, size, &read, NULL) && read > 0) {
        if (read >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
            /* UTF-16LE with BOM */
            int n = (int)((read - 2) / 2);
            int i;
            if (n > cchOut - 1) n = cchOut - 1;
            for (i = 0; i < n; i++) out[i] = (WCHAR)(buf[2 + i * 2] | (buf[2 + i * 2 + 1] << 8));
            out[n] = 0;
            ok = TRUE;
        } else {
            int n;
            if (read >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
                n = MultiByteToWideChar(CP_UTF8, 0, (const char*)buf + 3, (int)read - 3,
                                        out, cchOut - 1);
            } else {
                n = MultiByteToWideChar(CP_UTF8, 0, (const char*)buf, (int)read,
                                        out, cchOut - 1);
                if (n <= 0) {
                    n = MultiByteToWideChar(CP_ACP, 0, (const char*)buf, (int)read,
                                            out, cchOut - 1);
                }
            }
            out[n > 0 ? n : 0] = 0;
            ok = TRUE;
        }
    }

    free(buf);
    CloseHandle(h);
    return ok;
}

/* =====================  解析  ===================== */

static int StrToInt(const WCHAR* s, int def)
{
    WCHAR* end = NULL;
    long v;
    if (!s || !s[0]) return def;
    v = wcstol(s, &end, 10);
    if (end == s) return def;
    return (int)v;
}

static void ApplyEntry(Config* c, const WCHAR* sec, const WCHAR* key, const WCHAR* val)
{
    if (StrEqualI(sec, L"Schemes")) {
        if (StrEqualI(key, L"A")) {
            GUID g;
            if (GuidParse(val, &g)) { c->guidA = g; c->hasA = TRUE; }
        } else if (StrEqualI(key, L"B")) {
            GUID g;
            if (GuidParse(val, &g)) { c->guidB = g; c->hasB = TRUE; }
        }
    } else if (StrEqualI(sec, L"Labels")) {
        /* 兼容旧 ini：TextA/TextB 键已被忽略（提示名直接用方案真实名称） */
        if (StrEqualI(key, L"ColorA")) {
            c->colorA = ColorFromHex(val, c->colorA);
        } else if (StrEqualI(key, L"ColorB")) {
            c->colorB = ColorFromHex(val, c->colorB);
        } else if (StrEqualI(key, L"FgColor")) {
            c->fgColor = ColorFromHex(val, c->fgColor);
        } else if (StrEqualI(key, L"NeutralColor")) {
            c->neutralColor = ColorFromHex(val, c->neutralColor);
        }
    } else if (StrEqualI(sec, L"Idle")) {
        if (StrEqualI(key, L"Enabled")) {
            c->idleEnabled = (StrToInt(val, 1) != 0);
        } else if (StrEqualI(key, L"ThresholdSeconds")) {
            c->idleThresholdSec = StrToInt(val, c->idleThresholdSec);
        } else if (StrEqualI(key, L"Target")) {
            if (StrEqualI(val, L"B")) c->idleTarget = TARGET_B;
            else if (StrEqualI(val, L"custom")) c->idleTarget = TARGET_CUSTOM;
            else c->idleTarget = TARGET_A;
        } else if (StrEqualI(key, L"TargetCustomGuid")) {
            GUID g;
            if (GuidParse(val, &g)) { c->guidCustom = g; c->hasCustom = TRUE; }
        } else if (StrEqualI(key, L"RevertOnResume")) {
            c->revertOnResume = (StrToInt(val, 0) != 0);
        } else if (StrEqualI(key, L"CooldownSeconds")) {
            c->cooldownSec = StrToInt(val, c->cooldownSec);
        }
    } else if (StrEqualI(sec, L"General")) {
        if (StrEqualI(key, L"AutoStart")) {
            c->autoStart = (StrToInt(val, 0) != 0);
        } else if (StrEqualI(key, L"ShowBalloon")) {
            c->showBalloon = (StrToInt(val, 0) != 0);
        } else if (StrEqualI(key, L"FirstRunDone")) {
            c->firstRunDone = (StrToInt(val, 0) != 0);
        } else if (StrEqualI(key, L"LogLevel")) {
            c->logLevel = StrToInt(val, 0);
        }
    }
}

static void ParseText(const WCHAR* text, Config* c)
{
    WCHAR section[64];
    const WCHAR* p = text;

    section[0] = 0;

    while (*p) {
        WCHAR line[1024];
        int n = 0;

        while (*p && *p != L'\n' && n < 1023 && *p != L'\r') {
            line[n++] = *p++;
        }
        while (*p && *p != L'\n') p++;      /* 跳过 \r 等残留 */
        if (*p == L'\n') p++;
        line[n] = 0;
        StrTrim(line);

        if (!line[0] || line[0] == L';' || line[0] == L'#') continue;

        if (line[0] == L'[') {
            int len = lstrlenW(line);
            if (len > 2 && line[len - 1] == L']') line[len - 1] = 0;
            StrCopy(section, 64, line + 1);
            StrTrim(section);
            continue;
        }

        {
            WCHAR* eq = line;
            while (*eq && *eq != L'=') eq++;
            if (*eq != L'=') continue;
            *eq = 0;
            {
                WCHAR* key = line;
                WCHAR* val = eq + 1;
                StrTrim(key);
                StrTrim(val);
                ApplyEntry(c, section, key, val);
            }
        }
    }
}

/* =====================  对外接口  ===================== */

BOOL ConfigLoad(const WCHAR* path, Config* c)
{
    static WCHAR text[CONFIG_BUF_SIZE];

    if (!path || !c) return FALSE;
    ConfigDefaults(c);
    if (!ReadAllText(path, text, CONFIG_BUF_SIZE)) return FALSE;
    ParseText(text, c);

    if (c->idleThresholdSec < IDLE_MIN_SEC) c->idleThresholdSec = IDLE_MIN_SEC;
    if (c->idleThresholdSec > IDLE_MAX_SEC) c->idleThresholdSec = IDLE_MAX_SEC;
    if (c->cooldownSec < 0) c->cooldownSec = 0;
    if (c->cooldownSec > 600) c->cooldownSec = 600;

    return TRUE;
}

BOOL ConfigSave(const WCHAR* path, const Config* c)
{
    Sb sb;
    WCHAR hex[64];          /* GUID 字符串 38 字符 + 结尾 */
    HANDLE h;
    DWORD written = 0;
    WORD bom = 0xFEFF;
    int len;

    if (!path || !c) return FALSE;

    SbInit(&sb, g_saveBuf, CONFIG_BUF_SIZE);
    SbAppend(&sb, L"; ===========================================================\r\n");
    SbAppend(&sb, L"; PowerToggle 配置（UTF-16LE，可用记事本直接编辑）\r\n");
    SbAppend(&sb, L"; 手工修改后需重启程序生效\r\n");
    SbAppend(&sb, L"; ===========================================================\r\n\r\n");

    SbAppend(&sb, L"[Version]\r\nConfigVersion=1\r\n\r\n");

    SbAppend(&sb, L"[Schemes]\r\n; 两个参与 Toggle 的方案 GUID\r\n");
    SbAppend(&sb, L"A=");
    if (c->hasA) { GuidFormat(&c->guidA, hex, 64); SbAppend(&sb, hex); }
    SbAppend(&sb, L"\r\nB=");
    if (c->hasB) { GuidFormat(&c->guidB, hex, 64); SbAppend(&sb, hex); }
    SbAppend(&sb, L"\r\n\r\n");

    SbAppend(&sb, L"[Labels]\r\n; 悬停提示直接显示方案真实名称；此处仅存图标颜色\r\n");
    ColorToHex(c->colorA, hex, 16);
    SbAppendF(&sb, L"ColorA=%s\r\n", hex);
    ColorToHex(c->colorB, hex, 16);
    SbAppendF(&sb, L"ColorB=%s\r\n", hex);
    ColorToHex(c->fgColor, hex, 16);
    SbAppendF(&sb, L"FgColor=%s\r\n", hex);
    ColorToHex(c->neutralColor, hex, 16);
    SbAppendF(&sb, L"NeutralColor=%s\r\n\r\n", hex);

    SbAppend(&sb, L"[Idle]\r\n");
    SbAppendF(&sb, L"Enabled=%d\r\n", c->idleEnabled ? 1 : 0);
    SbAppendF(&sb, L"ThresholdSeconds=%d\r\n", c->idleThresholdSec);
    SbAppend(&sb, L"Target=");
    if (c->idleTarget == TARGET_B) SbAppend(&sb, L"B");
    else if (c->idleTarget == TARGET_CUSTOM) SbAppend(&sb, L"custom");
    else SbAppend(&sb, L"A");
    SbAppend(&sb, L"\r\nTargetCustomGuid=");
    if (c->hasCustom) { GuidFormat(&c->guidCustom, hex, 64); SbAppend(&sb, hex); }
    SbAppend(&sb, L"\r\n");
    SbAppendF(&sb, L"RevertOnResume=%d\r\n", c->revertOnResume ? 1 : 0);
    SbAppendF(&sb, L"CooldownSeconds=%d\r\n\r\n", c->cooldownSec);

    SbAppend(&sb, L"[General]\r\n");
    SbAppendF(&sb, L"AutoStart=%d\r\n", c->autoStart ? 1 : 0);
    SbAppendF(&sb, L"ShowBalloon=%d\r\n", c->showBalloon ? 1 : 0);
    SbAppendF(&sb, L"FirstRunDone=%d\r\n", c->firstRunDone ? 1 : 0);
    SbAppendF(&sb, L"LogLevel=%d\r\n", c->logLevel);

    len = sb.len;
    if (len <= 0) return FALSE;

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        LogFmt(1, L"无法写入配置文件: %s", path);
        return FALSE;
    }
    WriteFile(h, &bom, 2, &written, NULL);
    WriteFile(h, g_saveBuf, (DWORD)(len * sizeof(WCHAR)), &written, NULL);
    CloseHandle(h);
    return TRUE;
}

void ConfigResolvePath(WCHAR* fileOut, int cchFile, WCHAR* dirOut, int cchDir)
{
    WCHAR exeDir[MAX_PATH];
    WCHAR candidate[MAX_PATH];

    if (fileOut && cchFile > 0) fileOut[0] = 0;
    if (dirOut && cchDir > 0) dirOut[0] = 0;

    PathGetExeDir(exeDir, MAX_PATH);
    PathCombine(candidate, MAX_PATH, exeDir, L"config.ini");

    if (FileExists(candidate) || DirIsWritable(exeDir)) {
        if (fileOut) StrCopy(fileOut, cchFile, candidate);
        if (dirOut) StrCopy(dirOut, cchDir, exeDir);
        return;
    }

    {
        WCHAR appdata[MAX_PATH];
        WCHAR dir[MAX_PATH];
        WCHAR file[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
            PathCombine(dir, MAX_PATH, appdata, L"PowerToggle");
            DirEnsure(dir);
            PathCombine(file, MAX_PATH, dir, L"config.ini");
            if (fileOut) StrCopy(fileOut, cchFile, file);
            if (dirOut) StrCopy(dirOut, cchDir, dir);
            return;
        }
    }

    /* 兜底：仍在 exe 目录 */
    if (fileOut) StrCopy(fileOut, cchFile, candidate);
    if (dirOut) StrCopy(dirOut, cchDir, exeDir);
}
