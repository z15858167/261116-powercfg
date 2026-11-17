/* tray.cpp */
#include "tray.h"
#include "util.h"
#include "resource.h"

static NOTIFYICONDATAW g_nid;
static BOOL              g_added = FALSE;

BOOL TrayGetRect(RECT* rc)
{
    NOTIFYICONIDENTIFIER id;
    HRESULT hr;

    if (!rc || !g_added) return FALSE;
    ZeroMemory(&id, sizeof(id));
    id.cbSize = sizeof(id);
    id.hWnd  = g_nid.hWnd;
    id.uID   = g_nid.uID;
    hr = Shell_NotifyIconGetRect(&id, rc);
    return SUCCEEDED(hr) ? TRUE : FALSE;
}

BOOL TrayAdd(HWND hwnd)
{
    RECT rc;

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_NOTIFY;
    g_nid.hIcon            = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    StrCopy(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(WCHAR), APP_NAME);

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        /* 可能是上次异常退出留下的僵尸记录，先删再加 */
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
            LogFmt(1, L"Shell_NotifyIcon(NIM_ADD) 失败");
            return FALSE;
        }
    }

    g_added = TRUE;

    if (TrayGetRect(&rc)) {
        LogFmt(2, L"托盘图标已注册，位置 (%d,%d)-(%d,%d)",
               (int)rc.left, (int)rc.top, (int)rc.right, (int)rc.bottom);
    } else {
        LogFmt(1, L"托盘图标已注册，但 Shell_NotifyIconGetRect 未取到位置");
    }
    return TRUE;
}

void TrayRemove(void)
{
    if (!g_added) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_added = FALSE;
}

BOOL TraySetIcon(HICON icon, const WCHAR* tip)
{
    if (!g_added) return FALSE;
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    if (icon) g_nid.hIcon = icon;
    if (tip) StrCopy(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(WCHAR), tip);
    if (!Shell_NotifyIconW(NIM_MODIFY, &g_nid)) {
        LogFmt(1, L"Shell_NotifyIcon(NIM_MODIFY/NIF_ICON) 失败");
        /* 图标可能被资源管理器丢弃（Explorer 重启等），重试一次 ADD */
        if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
            LogFmt(2, L"托盘图标已重新添加");
        }
        return FALSE;
    }
    return TRUE;
}

BOOL TrayBalloon(const WCHAR* title, const WCHAR* text, DWORD flags)
{
    if (!g_added) return FALSE;
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = flags ? flags : NIIF_INFO;
    g_nid.uTimeout = 3000;
    StrCopy(g_nid.szInfoTitle, sizeof(g_nid.szInfoTitle) / sizeof(WCHAR),
            title ? title : APP_NAME);
    StrCopy(g_nid.szInfo, sizeof(g_nid.szInfo) / sizeof(WCHAR), text ? text : L"");
    return Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}
