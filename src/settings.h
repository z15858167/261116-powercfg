/* settings.h —— 设置窗口 */
#ifndef PT_SETTINGS_H
#define PT_SETTINGS_H

#include "ptdef.h"
#include <windows.h>
#include "config.h"

/* 打开模态设置窗口。
   cfg      : 输入输出，用户点"保存"后内容被覆盖
   cfgFile  : 配置文件路径，用于落盘
   firstRun : 首次配置模式（顶部显示引导文字）
   返回 TRUE 表示用户已保存 */
BOOL SettingsOpen(HINSTANCE hInst, HWND owner, Config* cfg,
                  const WCHAR* cfgFile, BOOL firstRun);

#endif /* PT_SETTINGS_H */
