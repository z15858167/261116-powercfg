/* test_main.cpp —— PowerToggle 单元/集成自测（控制台程序）
 * 只做只读或内存级操作，不会切换你的实际电源方案。
 */
#include "../src/ptdef.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../src/util.h"
#include "../src/power.h"
#include "../src/config.h"
#include "../src/idle.h"
#include "../src/icon.h"
#include "../src/tray.h"
#include "../src/version.h"

static int g_pass = 0;
static int g_fail = 0;

static void Check(BOOL cond, const char* name, int line)
{
    if (cond) {
        g_pass++;
        printf("[PASS] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s   (line %d)\n", name, line);
    }
}

#define CHECK(cond, name) Check((cond) ? TRUE : FALSE, name, __LINE__)

/* ---------- 配置 ---------- */

static void TestConfigDefaults(void)
{
    Config c;
    ConfigDefaults(&c);

    CHECK(c.idleEnabled == TRUE,       "config: idle enabled by default");
    CHECK(c.revertOnResume == FALSE,   "config: revert disabled by default");
    CHECK(c.idleThresholdSec == 600,   "config: default threshold 600s");
    CHECK(c.cooldownSec == 5,          "config: default cooldown 5s");
    CHECK(c.guidA.Data1 == 0,          "config: no scheme bound by default");
}

static void TestConfigRoundTrip(void)
{
    WCHAR tmp[MAX_PATH];
    WCHAR dir[MAX_PATH];
    WCHAR file[MAX_PATH];
    Config a, b;
    GUID   ga, gb;

    GetTempPathW(MAX_PATH, tmp);
    PathCombine(dir, MAX_PATH, tmp, L"PowerToggleTest");
    DirEnsure(dir);
    PathCombine(file, MAX_PATH, dir, L"config_roundtrip.ini");
    DeleteFileW(file);

    ConfigDefaults(&a);
    CHECK(GuidParse(L"{381b4222-f694-41f0-9685-ff5bb260df2e}", &ga), "guid: parse A");
    CHECK(GuidParse(L"{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}", &gb), "guid: parse B");

    a.hasA = TRUE;
    a.guidA = ga;
    a.hasB = TRUE;
    a.guidB = gb;
    a.colorA = RGB(0x11, 0x22, 0x33);
    a.colorB = RGB(0xAA, 0xBB, 0xCC);
    a.idleThresholdSec = 1234;
    a.idleTarget = TARGET_B;
    a.revertOnResume = TRUE;
    a.autoStart = TRUE;
    a.firstRunDone = TRUE;

    CHECK(ConfigSave(file, &a), "config: save ok");
    CHECK(FileExists(file),     "config: file written");

    CHECK(ConfigLoad(file, &b), "config: load ok");
    CHECK(b.hasA && GuidEqual(&b.guidA, &a.guidA), "config: guidA roundtrip");
    CHECK(b.hasB && GuidEqual(&b.guidB, &a.guidB), "config: guidB roundtrip");
    CHECK(b.colorA == a.colorA, "config: colorA roundtrip");
    CHECK(b.colorB == a.colorB, "config: colorB roundtrip");
    CHECK(b.idleThresholdSec == 1234, "config: threshold roundtrip");
    CHECK(b.idleTarget == TARGET_B,   "config: idle target roundtrip");
    CHECK(b.revertOnResume == TRUE,   "config: revert flag roundtrip");
    CHECK(b.autoStart == TRUE,        "config: autostart flag roundtrip");

    DeleteFileW(file);
}

static void TestConfigEdgeCases(void)
{
    WCHAR tmp[MAX_PATH];
    WCHAR dir[MAX_PATH];
    WCHAR file[MAX_PATH];
    WCHAR missing[MAX_PATH];
    Config c;

    GetTempPathW(MAX_PATH, tmp);
    PathCombine(dir, MAX_PATH, tmp, L"PowerToggleTest");
    DirEnsure(dir);
    PathCombine(file, MAX_PATH, dir, L"config_corrupt.ini");
    PathCombine(missing, MAX_PATH, dir, L"config_missing.ini");
    DeleteFileW(missing);

    CHECK(ConfigLoad(missing, &c) == FALSE, "config: missing file returns FALSE");
    CHECK(c.hasA == FALSE,                  "config: missing file keeps defaults");

    /* 写入垃圾内容 */
    {
        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        const char* junk = "[Schemes]\r\nA=not-a-guid\r\nB=\r\n[Idle]\r\nThresholdSeconds=abc\r\n";
        DWORD w = 0;
        if (h != INVALID_HANDLE_VALUE) {
            WriteFile(h, junk, (DWORD)lstrlenA(junk), &w, NULL);
            CloseHandle(h);
        }
    }

    CHECK(ConfigLoad(file, &c) == TRUE, "config: corrupt file still loads");
    CHECK(c.hasA == FALSE,              "config: invalid guid ignored");
    CHECK(c.idleThresholdSec >= IDLE_MIN_SEC &&
          c.idleThresholdSec <= IDLE_MAX_SEC, "config: threshold clamped");

    DeleteFileW(file);
}

/* ---------- 电源方案（只读） ---------- */

static void TestPower(void)
{
    SchemeList list;
    GUID       cur;
    BOOL       hasCur;
    WCHAR      buf[64];
    GUID       round;
    int        i;

    CHECK(PowerEnumSchemes(&list) == TRUE, "power: enumerate returns TRUE");
    CHECK(list.count > 0,                  "power: at least one scheme");
    printf("       (%d schemes found)\n", list.count);

    for (i = 0; i < list.count; i++) {
        char utf8[256];
        WideCharToMultiByte(CP_UTF8, 0, list.items[i].name, -1, utf8, sizeof(utf8), NULL, NULL);
        printf("       [%d] %s\n", i, utf8);
    }

    hasCur = PowerGetActiveGuid(&cur);
    CHECK(hasCur == TRUE, "power: read active scheme");
    CHECK(PowerFindByGuid(&list, &cur, NULL) == TRUE,
          "power: active scheme is in enumerate list");

    GuidFormat(&cur, buf, 64);
    CHECK(GuidParse(buf, &round) == TRUE, "guid: format -> parse ok");
    CHECK(GuidEqual(&cur, &round),        "guid: roundtrip equal");

    CHECK(GuidParse(L"{not-a-guid}", &round) == FALSE, "guid: reject invalid text");
    CHECK(GuidParse(L"", &round) == FALSE,            "guid: reject empty text");
}

/* ---------- 空闲状态机 ---------- */

static void TestIdleMachine(void)
{
    Config     c;
    IdleEngine e;
    GUID       ga, gb, gc, out;
    int        r;
    ULONGLONG  t;

    GuidParse(L"{381b4222-f694-41f0-9685-ff5bb260df2e}", &ga);
    GuidParse(L"{8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}", &gb);
    GuidParse(L"{a1841308-3541-4fab-bc81-f71556f20b4a}", &gc);

    ConfigDefaults(&c);
    c.hasA = TRUE; c.guidA = ga;
    c.hasB = TRUE; c.guidB = gb;
    c.idleEnabled = TRUE;
    c.idleThresholdSec = 60;
    c.cooldownSec = 5;
    c.revertOnResume = FALSE;

    /* 1) 未达阈值不动作 */
    IdleInit(&e);
    e.manualTick = 1000;
    e.cooldownUntil = 0;
    r = IdleStep(&e, &c, 31000, 30000, &ga, TRUE, &gb, TRUE, &out);
    CHECK(r == IDLE_NONE, "idle: below threshold does nothing");

    /* 2) 达到阈值切到目标 */
    r = IdleStep(&e, &c, 62000, 61000, &ga, TRUE, &gb, TRUE, &out);
    CHECK(r == IDLE_APPLY, "idle: above threshold applies");
    CHECK(GuidEqual(&out, &ga), "idle: applies target scheme A");
    CHECK(e.autoApplied == TRUE, "idle: marked as auto applied");
    CHECK(e.hasSnapshot == TRUE && GuidEqual(&e.snapshot, &gb), "idle: snapshot recorded");
    CHECK(GuidEqual(&e.logFrom, &gb) && GuidEqual(&e.logTo, &ga) && e.logIdleSec == 61,
          "idle: apply records from/to and idle seconds for logging");

    /* 3) 用户回来但未开启切回 */
    r = IdleStep(&e, &c, 63000, 0, &ga, TRUE, &ga, TRUE, &out);
    CHECK(r == IDLE_NONE, "idle: resume without revert keeps target");
    CHECK(e.autoApplied == FALSE, "idle: state cleared after resume");

    /* 4) 目标 == 当前，不切换 */
    IdleInit(&e);
    e.manualTick = 0;
    e.cooldownUntil = 0;
    r = IdleStep(&e, &c, 100000, 99000, &ga, TRUE, &ga, TRUE, &out);
    CHECK(r == IDLE_NONE, "idle: target equals current -> no switch");

    /* 5) 开启切回 */
    c.revertOnResume = TRUE;
    IdleInit(&e);
    e.manualTick = 0;
    e.cooldownUntil = 0;
    r = IdleStep(&e, &c, 62000, 61000, &ga, TRUE, &gb, TRUE, &out);
    CHECK(r == IDLE_APPLY && GuidEqual(&out, &ga), "idle: revert-on apply first");
    r = IdleStep(&e, &c, 63000, 0, &ga, TRUE, &ga, TRUE, &out);
    CHECK(r == IDLE_REVERT, "idle: resume triggers revert");
    CHECK(GuidEqual(&out, &gb), "idle: revert goes back to previous scheme");
    CHECK(e.cooldownUntil > 63000, "idle: cooldown armed after revert");
    CHECK(GuidEqual(&e.logFrom, &ga) && GuidEqual(&e.logTo, &gb) && e.logIdleSec == 0,
          "idle: revert records from/to with zero idle seconds");

    /* 6) 当前方案被外部改动，快照失效不回切 */
    IdleInit(&e);
    e.manualTick = 0;
    e.cooldownUntil = 0;
    r = IdleStep(&e, &c, 62000, 61000, &ga, TRUE, &gb, TRUE, &out);
    CHECK(r == IDLE_APPLY, "idle: apply before external change");
    r = IdleStep(&e, &c, 63000, 0, &ga, TRUE, &gc, TRUE, &out);
    CHECK(r == IDLE_NONE, "idle: no revert when scheme changed externally");

    /* 7) 手动切换重置计时 */
    c.revertOnResume = FALSE;
    IdleInit(&e);
    IdleOnManualSwitch(&e);
    t = GetTickCount64();
    r = IdleStep(&e, &c, t + 1000, 61000, &ga, TRUE, &gb, TRUE, &out);
    CHECK(r == IDLE_NONE, "idle: manual switch resets idle timer");

    /* 8) 功能关闭时不动作 */
    c.idleEnabled = FALSE;
    IdleInit(&e);
    e.manualTick = 0;
    e.cooldownUntil = 0;
    r = IdleStep(&e, &c, 999999, 900000, &ga, TRUE, &gb, TRUE, &out);
    CHECK(r == IDLE_NONE, "idle: disabled does nothing");

    /* 9) 冷却期内不重复切换 */
    c.idleEnabled = TRUE;
    IdleInit(&e);
    e.manualTick = 0;
    e.cooldownUntil = 100000;
    r = IdleStep(&e, &c, 99000, 98000, &ga, TRUE, &gb, TRUE, &out);
    CHECK(r == IDLE_NONE, "idle: cooldown suppresses switch");
}

/* ---------- 空闲切换专用日志 ---------- */

static DWORD LogFileSize(const WCHAR* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD sz = 0;
    if (h != INVALID_HANDLE_VALUE) {
        sz = GetFileSize(h, NULL);
        CloseHandle(h);
    }
    return sz;
}

static DWORD LogFileRead(const WCHAR* path, char* buf, int cch)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD read = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ZeroMemory(buf, cch);
    ReadFile(h, buf, cch - 1, &read, NULL);
    CloseHandle(h);
    return read;
}

static void TestIdleLog(void)
{
    WCHAR tmp[MAX_PATH], dir[MAX_PATH], file[MAX_PATH], generic[MAX_PATH];
    char  buf[8192];
    DWORD read, size1, written;
    HANDLE h;

    GetTempPathW(MAX_PATH, tmp);
    PathCombine(dir, MAX_PATH, tmp, L"PowerToggleTest");
    DirEnsure(dir);
    PathCombine(file, MAX_PATH, dir, L"idle_switch_test.log");
    PathCombine(generic, MAX_PATH, dir, L"generic_log_test.log");
    DeleteFileW(file);
    DeleteFileW(generic);

    /* 专用日志必须在通用日志关闭时也能写入 */
    LogSetLevel(0);
    LogSetIdlePath(file);
    IdleLog(L"IDLELOGTEST marker=42");
    CHECK(FileExists(file), "idlelog: dedicated file created");

    read = LogFileRead(file, buf, sizeof(buf));
    CHECK(read > 0 && strstr(buf, "IDLELOGTEST marker=42") != NULL,
          "idlelog: entry written with LogLevel=0");

    size1 = LogFileSize(file);
    IdleLog(L"IDLELOGTEST second=7");
    CHECK(LogFileSize(file) > size1, "idlelog: entries append");

    /* 两条日志互不串写：通用日志不得写进专用文件 */
    LogSetPath(generic);
    LogSetLevel(2);
    LogFmt(2, L"IDLELOGTEST generic-only");
    read = LogFileRead(file, buf, sizeof(buf));
    CHECK(strstr(buf, "generic-only") == NULL, "idlelog: generic log does not leak in");
    read = LogFileRead(generic, buf, sizeof(buf));
    CHECK(read > 0 && strstr(buf, "IDLELOGTEST generic-only") != NULL,
          "idlelog: generic log still writes to its own file");

    /* 超过通用日志 64KB 上限后仍只追加，不清空历史 */
    h = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        char filler[4096];
        int i;
        memset(filler, 'X', sizeof(filler));
        for (i = 0; i < 18; i++) WriteFile(h, filler, sizeof(filler), &written, NULL);
        CloseHandle(h);
    }
    size1 = LogFileSize(file);
    CHECK(size1 > 64 * 1024, "idlelog: test file exceeds generic rotation threshold");
    IdleLog(L"IDLELOGTEST after=99");
    CHECK(LogFileSize(file) > size1, "idlelog: no truncation past 64KB");

    LogSetPath(NULL);
    LogSetLevel(0);
    LogSetIdlePath(NULL);
    DeleteFileW(file);
    DeleteFileW(generic);
}

/* ---------- 图标生成 ---------- */

static void TestIconBuild(void)
{
    HICON ic;
    BITMAPINFO bi;
    HDC screen, mem;
    HBITMAP bmp, old;
    void* bits = NULL;
    int x, y, nonEmpty = 0, green = 0, white = 0;

    ic = IconBuild(RGB(0x2E, 0x7D, 0x32), RGB(0xFF, 0xFF, 0xFF), 16);
    CHECK(ic != NULL, "icon: build 16px lightning icon");
    if (!ic) return;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = 16;
    bi.bmiHeader.biHeight      = -16;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    old = (HBITMAP)SelectObject(mem, bmp);
    ZeroMemory(bits, 16 * 16 * 4);

    DrawIconEx(mem, 0, 0, ic, 16, 16, 0, NULL, DI_NORMAL);

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            BYTE* p = (BYTE*)bits + (y * 16 + x) * 4;
            if (p[0] | p[1] | p[2]) nonEmpty++;
            if (p[1] > p[2] + 20 && p[1] > 40) green++;
            if (p[0] > 200 && p[1] > 200 && p[2] > 200) white++;
        }
    }

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    DestroyIcon(ic);

    CHECK(nonEmpty > 120, "icon: most of the 16x16 area is painted (not blank)");
    CHECK(green > 80,     "icon: configured background color is actually used");
    CHECK(white > 8,      "icon: white lightning bolt is drawn");
}

/* 把图标渲染成 BGRA 缓冲区，便于逐像素比较 */
static BOOL IconPixels(HICON ic, int size, BYTE* out)
{
    BITMAPINFO bi;
    HDC screen, mem;
    HBITMAP bmp, old;
    void* bits = NULL;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = size;
    bi.bmiHeader.biHeight      = -size;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!mem || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        return FALSE;
    }

    old = (HBITMAP)SelectObject(mem, bmp);
    ZeroMemory(bits, (size_t)size * size * 4);
    DrawIconEx(mem, 0, 0, ic, size, size, 0, NULL, DI_NORMAL);
    memcpy(out, bits, (size_t)size * size * 4);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    return TRUE;
}

/* 托盘图标靠形状+底色区分状态：闪电形状固定，A/B 底色不同则像素不同 */
static void TestIconColorStates(void)
{
    static BYTE a[16 * 16 * 4], b[16 * 16 * 4], c[16 * 16 * 4], d[16 * 16 * 4];
    HICON i1, i2, i3, i4;

    i1 = IconBuild(RGB(0x2E, 0x7D, 0x32), RGB(255, 255, 255), 16);  /* 方案 A 绿 */
    i2 = IconBuild(RGB(0xC6, 0x28, 0x28), RGB(255, 255, 255), 16);  /* 方案 B 红 */
    CHECK(i1 && i2, "icon: build A/B color icons");
    if (i1 && i2 && IconPixels(i1, 16, a) && IconPixels(i2, 16, b)) {
        CHECK(memcmp(a, b, sizeof(a)) != 0,
              "icon: different A/B backgrounds produce different icons");
    }
    if (i1) DestroyIcon(i1);
    if (i2) DestroyIcon(i2);

    /* 中性态与异常态的灰色底同样可渲染 */
    i3 = IconBuild(RGB(0x6B, 0x72, 0x80), RGB(255, 255, 255), 16);
    i4 = IconBuild(RGB(0x60, 0x60, 0x60), RGB(255, 255, 255), 16);
    CHECK(i3 && i4, "icon: build neutral/error icons");
    if (i3) DestroyIcon(i3);
    if (i4) DestroyIcon(i4);

    /* 前景色（闪电颜色）参与渲染 */
    {
        HICON i5 = IconBuild(RGB(0x2E, 0x7D, 0x32), RGB(255, 255, 0), 16);
        HICON i6 = IconBuild(RGB(0x2E, 0x7D, 0x32), RGB(255, 255, 255), 16);
        CHECK(i5 && i6, "icon: build fg-variant icons");
        if (i5 && i6 && IconPixels(i5, 16, c) && IconPixels(i6, 16, d)) {
            CHECK(memcmp(c, d, sizeof(c)) != 0,
                  "icon: fg color changes the bolt");
        }
        if (i5) DestroyIcon(i5);
        if (i6) DestroyIcon(i6);
    }
}

/* ---------- 托盘注册（会短暂出现一个图标） ---------- */

static LRESULT CALLBACK TrayTestProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcW(h, m, w, l);
}

static void TestTrayRegister(void)
{
    WNDCLASSEXW wc;
    HWND hwnd;
    HICON ic;
    RECT rc;
    MSG msg;
    DWORD deadline;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = TrayTestProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = L"PTTrayTestWnd";
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(0, L"PTTrayTestWnd", L"traytest", WS_POPUP,
                           0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    CHECK(hwnd != NULL, "tray: test window created");
    if (!hwnd) return;

    CHECK(TrayAdd(hwnd) == TRUE, "tray: Shell_NotifyIcon(NIM_ADD) succeeded");

    ic = IconBuild(RGB(0x2E, 0x7D, 0x32), RGB(255, 255, 255), 16);
    CHECK(TraySetIcon(ic, L"PowerToggle 自检") == TRUE, "tray: NIM_MODIFY with icon accepted");

    deadline = GetTickCount() + 1500;
    while (GetTickCount() < deadline) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(30);
    }

    ZeroMemory(&rc, sizeof(rc));
    CHECK(TrayGetRect(&rc) == TRUE, "tray: shell reports an icon rectangle");
    CHECK(rc.right > rc.left && rc.bottom > rc.top, "tray: icon rectangle is non-empty");
    printf("       tray rect: %ld,%ld - %ld,%ld\n",
           (long)rc.left, (long)rc.top, (long)rc.right, (long)rc.bottom);

    TrayRemove();
    if (ic) DestroyIcon(ic);
    DestroyWindow(hwnd);
}

/* ---------- 显示标签 ---------- */

static void TestLabels(void)
{
    WCHAR b[96];

    StrFormatLabel(b, 96, L"方案 A", L"节能");
    CHECK(lstrcmpW(b, L"方案 A（节能）") == 0, "label: base plus scheme name");

    StrFormatLabel(b, 96, L"方案 A", L"");
    CHECK(lstrcmpW(b, L"方案 A") == 0, "label: empty detail keeps base");

    StrFormatLabel(b, 96, L"方案 A", NULL);
    CHECK(lstrcmpW(b, L"方案 A") == 0, "label: null detail keeps base");

    StrFormatLabel(b, 48, L"方案 A", L"未选择");
    CHECK(lstrcmpW(b, L"方案 A（未选择）") == 0, "label: unselected hint");
}

/* ---------- 版本号 ---------- */

static void TestVersion(void)
{
    WCHAR want[32];
    char  wantA[32];

    CHECK(PT_VER_PATCH > 0, "version: build script bumped patch above zero");

    wsprintfW(want, L"%d.%d.%d", PT_VER_MAJOR, PT_VER_MINOR, PT_VER_PATCH);
    CHECK(lstrcmpW(PT_VER_WSTR, want) == 0, "version: wide string agrees with numbers");

    wsprintfA(wantA, "%d.%d.%d", PT_VER_MAJOR, PT_VER_MINOR, PT_VER_PATCH);
    CHECK(lstrcmpA(PT_VER_STR, wantA) == 0, "version: narrow string agrees with numbers");
}

int wmain(int argc, wchar_t** argv)
{
    (void)argc;
    (void)argv;

    printf("== PowerToggle tests ==\n\n");

    TestConfigDefaults();
    printf("\n");
    TestConfigRoundTrip();
    printf("\n");
    TestConfigEdgeCases();
    printf("\n");
    TestPower();
    printf("\n");
    TestIdleMachine();
    printf("\n");
    TestIdleLog();
    printf("\n");
    TestLabels();
    printf("\n");
    TestVersion();
    printf("\n");
    TestIconBuild();
    printf("\n");
    TestIconColorStates();
    printf("\n");
    TestTrayRegister();

    printf("\n== result: %d passed, %d failed ==\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
