/* main.cpp —— 程序入口、隐藏主窗、消息循环、状态机调度 */
#include "ptdef.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "util.h"
#include "power.h"
#include "config.h"
#include "icon.h"
#include "tray.h"
#include "idle.h"
#include "autostart.h"
#include "settings.h"
#include "resource.h"
#include "version.h"

#define WM_APP_SHOWSETTINGS  (WM_APP + 20)
#define TIMER_TICK_ID        1

static const WCHAR kClassName[] = L"PowerToggleHiddenWnd";

#define KIND_A            0
#define KIND_B            1
#define KIND_NEUTRAL      2
#define KIND_ERROR        3   /* 配置过，但方案已从系统删除 */
#define KIND_UNCONFIGURED 4   /* 还没配置过，不要显示成"坏掉了" */

struct AppState {
    HINSTANCE  hInst;
    HWND       hwnd;
    Config     cfg;
    WCHAR      cfgFile[MAX_PATH];
    WCHAR      cfgDir[MAX_PATH];
    WCHAR      exePath[MAX_PATH];
    SchemeList schemes;
    GUID       current;
    BOOL       hasCurrent;
    WCHAR      currentName[SCHEME_NAME_MAX];
    BOOL       validA;
    BOOL       validB;
    BOOL       validCustom;
    HICON      icon;
    WCHAR      iconKey[160];
    IdleEngine idle;
    UINT       taskbarMsg;
    int        tick;
    BOOL       settingsOpen;
};

static AppState g;

/* ============ 状态读取 ============ */

static void ReadCurrent(void)
{
    SchemeInfo si;

    g.hasCurrent = PowerGetActiveGuid(&g.current);
    g.currentName[0] = 0;
    if (g.hasCurrent && PowerFindByGuid(&g.schemes, &g.current, &si)) {
        StrCopy(g.currentName, SCHEME_NAME_MAX, si.name);
    }
}

/* 从已枚举的方案列表里取显示名；找不到（方案失效）显示 ? */
static void SchemeNameOf(const GUID* guid, WCHAR* out, int cch)
{
    SchemeInfo si;

    if (guid && PowerFindByGuid(&g.schemes, guid, &si)) {
        StrCopy(out, cch, si.name);
    } else {
        StrCopy(out, cch, L"?");
    }
}

static void ValidateSchemes(void)
{
    PowerEnumSchemes(&g.schemes);
    g.validA = (g.cfg.hasA && PowerFindByGuid(&g.schemes, &g.cfg.guidA, NULL)) ? TRUE : FALSE;
    g.validB = (g.cfg.hasB && PowerFindByGuid(&g.schemes, &g.cfg.guidB, NULL)) ? TRUE : FALSE;
    g.validCustom = (g.cfg.hasCustom &&
                     PowerFindByGuid(&g.schemes, &g.cfg.guidCustom, NULL)) ? TRUE : FALSE;
}

/* ============ 图标与提示 ============ */

static void UpdateIcon(BOOL force)
{
    WCHAR     key[160];
    WCHAR     tip[512];
    COLORREF  bg;
    int       kind;
    BOOL      rebuild;

    bg = g.cfg.neutralColor;

    if (!g.cfg.hasA && !g.cfg.hasB) {
        kind = KIND_UNCONFIGURED;
        bg = RGB(0x37, 0x47, 0x4F);
    } else if (!g.validA && !g.validB) {
        kind = KIND_ERROR;
        bg = RGB(0x60, 0x60, 0x60);
    } else if (g.hasCurrent && g.validA && GuidEqual(&g.current, &g.cfg.guidA)) {
        kind = KIND_A;
        bg = g.cfg.colorA;
    } else if (g.hasCurrent && g.validB && GuidEqual(&g.current, &g.cfg.guidB)) {
        kind = KIND_B;
        bg = g.cfg.colorB;
    } else {
        kind = KIND_NEUTRAL;
    }

    /* 图标只靠形状+底色区分状态，tooltip 直接用方案真实名称 */
    wsprintfW(key, L"%d|%06X", kind, (unsigned)bg);

    if (kind == KIND_UNCONFIGURED) {
        StrCopy(tip, 512, L"尚未配置：右键 → 设置，选择两个电源方案");
    } else if (kind == KIND_ERROR) {
        StrCopy(tip, 512, L"电源方案已失效，请右键 → 设置 重新选择");
    } else {
        WCHAR nameA[SCHEME_NAME_MAX];
        WCHAR nameB[SCHEME_NAME_MAX];
        WCHAR curLabel[SCHEME_NAME_MAX + 8];
        const WCHAR* otherName;

        SchemeNameOf(&g.cfg.guidA, nameA, SCHEME_NAME_MAX);
        SchemeNameOf(&g.cfg.guidB, nameB, SCHEME_NAME_MAX);

        if (kind == KIND_A)       StrCopy(curLabel, SCHEME_NAME_MAX + 8, nameA);
        else if (kind == KIND_B)  StrCopy(curLabel, SCHEME_NAME_MAX + 8, nameB);
        else if (g.currentName[0]) StrCopy(curLabel, SCHEME_NAME_MAX + 8, g.currentName);
        else                       StrCopy(curLabel, SCHEME_NAME_MAX + 8, L"未知");

        if (kind == KIND_A) otherName = nameB;
        else                otherName = nameA;

        wsprintfW(tip, L"当前：%s　单击切换到 %s", curLabel, otherName);
    }

    rebuild = (force || !g.icon || lstrcmpW(key, g.iconKey) != 0) ? TRUE : FALSE;

    if (rebuild) {
        HICON ni = IconBuild(bg, g.cfg.fgColor, IconGetSmallSize(g.hwnd));
        if (ni) {
            if (g.icon) DestroyIcon(g.icon);
            g.icon = ni;
        }
        StrCopy(g.iconKey, 160, key);
    }

    TraySetIcon(g.icon, tip);
}

/* ============ 配置生效 ============ */

/* 通用日志与空闲切换专用日志都放在配置目录；专用日志路径只需设置一次即可长期有效。 */
static void SetupLogPaths(void)
{
    WCHAR path[MAX_PATH];

    PathCombine(path, MAX_PATH, g.cfgDir, L"PowerToggle.log");
    LogSetPath(path);
    LogSetLevel(g.cfg.logLevel);

    PathCombine(path, MAX_PATH, g.cfgDir, L"PowerToggle-idle.log");
    LogSetIdlePath(path);
}

static void ApplyConfig(void)
{
    SetupLogPaths();

    AutoStartApply(g.cfg.autoStart, g.exePath);
    IdleOnConfigChanged(&g.idle);

    ValidateSchemes();
    ReadCurrent();
    UpdateIcon(TRUE);
}

static void OpenSettings(BOOL firstRun)
{
    if (g.settingsOpen) return;
    g.settingsOpen = TRUE;
    if (SettingsOpen(g.hInst, g.hwnd, &g.cfg, g.cfgFile, firstRun)) {
        ApplyConfig();
        if (firstRun) {
            TrayBalloon(APP_NAME,
                        L"已开始常驻托盘：左键单击图标切换方案，右键打开菜单。"
                        L"若看不到图标，请点任务栏的 ^ 展开，把它拖到任务栏上（只需一次）。",
                        NIIF_INFO);
        }
    }
    g.settingsOpen = FALSE;
}

/* ============ 手动切换 ============ */

static void ShowSwitchError(DWORD code)
{
    WCHAR err[256];
    WCHAR msg[512];
    ErrText(code, err, 256);
    wsprintfW(msg, L"切换电源方案失败：%s", err);
    if (!TrayBalloon(APP_NAME, msg, NIIF_WARNING)) {
        MessageBoxW(NULL, msg, APP_NAME, MB_ICONERROR | MB_OK);
    }
}

static void DoToggle(void)
{
    GUID  target;
    BOOL  hasTarget = FALSE;
    DWORD r;

    if (!g.cfg.hasA && !g.cfg.hasB) {
        TrayBalloon(APP_NAME, L"还没有配置电源方案，先选两个方案再点我", NIIF_INFO);
        OpenSettings(TRUE);
        return;
    }
    if (!g.validA && !g.validB) {
        MessageBoxW(NULL, L"配置的电源方案已失效，请重新选择。", APP_NAME,
                    MB_ICONWARNING | MB_OK);
        OpenSettings(FALSE);
        return;
    }

    if (g.validA && g.validB) {
        if (g.hasCurrent && GuidEqual(&g.current, &g.cfg.guidA)) target = g.cfg.guidB;
        else target = g.cfg.guidA;
        hasTarget = TRUE;
    } else if (g.validA) {
        target = g.cfg.guidA;
        hasTarget = TRUE;
    } else {
        target = g.cfg.guidB;
        hasTarget = TRUE;
    }

    if (!hasTarget) return;

    IdleOnManualSwitch(&g.idle);

    r = PowerSetActiveGuid(&target);
    if (r != ERROR_SUCCESS) {
        ShowSwitchError(r);
        return;
    }

    ReadCurrent();
    UpdateIcon(TRUE);

    if (g.cfg.showBalloon) {
        TrayBalloon(APP_NAME, L"电源方案已切换", NIIF_INFO);
    }
}

/* ============ 菜单 ============ */

static void ToggleAutoStart(void)
{
    BOOL on = g.cfg.autoStart ? FALSE : TRUE;
    if (!AutoStartApply(on, g.exePath)) {
        MessageBoxW(NULL, L"写入开机自启项失败。", APP_NAME, MB_ICONERROR | MB_OK);
        return;
    }
    g.cfg.autoStart = on;
    ConfigSave(g.cfgFile, &g.cfg);
}

static void ShowTrayMenu(void)
{
    HMENU m;
    POINT pt;

    m = CreatePopupMenu();
    if (!m) return;

    AppendMenuW(m, MF_STRING | MF_DISABLED | MF_GRAYED, 0,
                L"PowerToggle v" PT_VER_WSTR);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"设置(&S)...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g.cfg.autoStart ? MF_CHECKED : MF_UNCHECKED),
                IDM_AUTOSTART, L"开机自启");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"退出(&X)");

    GetCursorPos(&pt);
    SetForegroundWindow(g.hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, g.hwnd, NULL);
    DestroyMenu(m);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
}

/* ============ 定时器 ============ */

static BOOL ResolveIdleTarget(GUID* out)
{
    if (!out) return FALSE;
    if (g.cfg.idleTarget == TARGET_B) {
        if (!g.validB) return FALSE;
        *out = g.cfg.guidB;
        return TRUE;
    }
    if (g.cfg.idleTarget == TARGET_CUSTOM) {
        if (!g.validCustom) return FALSE;
        *out = g.cfg.guidCustom;
        return TRUE;
    }
    if (!g.validA) return FALSE;
    *out = g.cfg.guidA;
    return TRUE;
}

/* 空闲切换成功后写专用日志：本地时间 + 方案名与 GUID。
   只在 PowerSetActiveGuid 成功后调用，避免失败时留下误导性记录。 */
static void LogIdleSwitch(int type, const GUID* from, const GUID* to, DWORD idleSec)
{
    WCHAR fromName[SCHEME_NAME_MAX];
    WCHAR toName[SCHEME_NAME_MAX];
    WCHAR fromGuid[64];
    WCHAR toGuid[64];

    SchemeNameOf(from, fromName, SCHEME_NAME_MAX);
    SchemeNameOf(to, toName, SCHEME_NAME_MAX);
    GuidFormat(from, fromGuid, 64);
    GuidFormat(to, toGuid, 64);

    if (type == IDLE_APPLY) {
        IdleLog(L"空闲切出 | %s %s → %s %s | 空闲 %u 秒",
                fromName, fromGuid, toName, toGuid, (unsigned)idleSec);
    } else {
        IdleLog(L"恢复回切 | %s %s → %s %s | 用户恢复操作",
                fromName, fromGuid, toName, toGuid);
    }
}

static void OnTick(void)
{
    GUID target;
    GUID outGuid;
    BOOL tv;
    int  r;

    g.tick++;

    if (g.tick % 2 == 0) {
        ReadCurrent();
        UpdateIcon(FALSE);
    }
    if (g.tick % 30 == 0) {
        ValidateSchemes();
        UpdateIcon(TRUE);
    }

    tv = ResolveIdleTarget(&target);
    r = IdleStep(&g.idle, &g.cfg, GetTickCount64(), IdleGetSystemMs(),
                 &target, tv, &g.current, g.hasCurrent, &outGuid);

    if (r == IDLE_APPLY || r == IDLE_REVERT) {
        if (PowerSetActiveGuid(&outGuid) == ERROR_SUCCESS) {
            LogIdleSwitch(r, &g.idle.logFrom, &g.idle.logTo, g.idle.logIdleSec);
            ReadCurrent();
            UpdateIcon(TRUE);
        } else {
            LogFmt(1, L"空闲自动切换失败");
        }
    }
}

/* ============ 窗口过程 ============ */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g.taskbarMsg != 0 && msg == g.taskbarMsg) {
        TrayAdd(hwnd);
        UpdateIcon(TRUE);
        return 0;
    }

    switch (msg) {
    case WM_TRAY_NOTIFY:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            DoToggle();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            return 0;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_SETTINGS:
            OpenSettings(FALSE);
            return 0;
        case IDM_AUTOSTART:
            ToggleAutoStart();
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_TICK_ID) {
            OnTick();
            return 0;
        }
        return 0;

    case WM_APP_SHOWSETTINGS:
        OpenSettings(wParam ? TRUE : FALSE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TICK_ID);
        TrayRemove();
        if (g.icon) { DestroyIcon(g.icon); g.icon = NULL; }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ============ 入口 ============ */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmdLine, int nShow)
{
    HANDLE          mtx;
    HWND            wnd;
    MSG             msg;
    WNDCLASSEXW     wc;
    INITCOMMONCONTROLSEX icc;

    (void)hPrev;
    (void)cmdLine;
    (void)nShow;

    mtx = CreateMutexW(NULL, TRUE, L"Local\\PowerToggle.SingleInstance");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(kClassName, NULL);
        if (prev) PostMessageW(prev, WM_APP_SHOWSETTINGS, 0, 0);
        CloseHandle(mtx);
        return 0;
    }

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ZeroMemory(&g, sizeof(g));
    g.hInst = hInst;

    PathGetExePath(g.exePath, MAX_PATH);
    ConfigResolvePath(g.cfgFile, MAX_PATH, g.cfgDir, MAX_PATH);
    ConfigLoad(g.cfgFile, &g.cfg);

    SetupLogPaths();

    g.taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kClassName;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    if (!RegisterClassExW(&wc)) return 1;

    wnd = CreateWindowExW(0, kClassName, APP_NAME, WS_POPUP,
                          0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (!wnd) return 1;
    g.hwnd = wnd;

    AutoStartSync(g.exePath);

    ValidateSchemes();
    ReadCurrent();

    if (!TrayAdd(wnd)) {
        MessageBoxW(NULL, L"托盘图标注册失败，程序无法继续运行。",
                    APP_NAME, MB_ICONERROR | MB_OK);
        DestroyWindow(wnd);
        return 1;
    }

    IdleInit(&g.idle);
    UpdateIcon(TRUE);
    SetTimer(wnd, TIMER_TICK_ID, 1000, NULL);

    if (!g.cfg.firstRunDone || !g.validA || !g.validB) {
        PostMessageW(wnd, WM_APP_SHOWSETTINGS, 1, 0);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mtx) CloseHandle(mtx);
    return (int)msg.wParam;
}
