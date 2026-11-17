/* autostart.h —— 开机自启（HKCU\...\Run） */
#ifndef PT_AUTOSTART_H
#define PT_AUTOSTART_H

#include "ptdef.h"
#include <windows.h>

BOOL AutoStartIsOn(void);
BOOL AutoStartApply(BOOL on, const WCHAR* exePath);

/* 若已开启但注册表里记录的路径与当前 exe 不一致，则自动更新 */
void AutoStartSync(const WCHAR* exePath);

#endif /* PT_AUTOSTART_H */
