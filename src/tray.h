/* tray.h —— 托盘图标注册与更新 */
#ifndef PT_TRAY_H
#define PT_TRAY_H

#include "ptdef.h"
#include <windows.h>
#include <shellapi.h>

#define WM_TRAY_NOTIFY   (WM_APP + 1)

BOOL TrayAdd(HWND hwnd);
void TrayRemove(void);
BOOL TraySetIcon(HICON icon, const WCHAR* tip);
BOOL TrayBalloon(const WCHAR* title, const WCHAR* text, DWORD flags);

/* 查询托盘图标的屏幕矩形，用于确认图标确实被资源管理器接纳 */
BOOL TrayGetRect(RECT* rc);

#endif /* PT_TRAY_H */
