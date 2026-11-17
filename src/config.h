/* config.h —— 配置结构与 ini 读写 */
#ifndef PT_CONFIG_H
#define PT_CONFIG_H

#include "ptdef.h"
#include <windows.h>

/* 空闲后切换目标 */
#define TARGET_A      0
#define TARGET_B      1
#define TARGET_CUSTOM 2

/* 阈值范围（秒） */
#define IDLE_MIN_SEC  10
#define IDLE_MAX_SEC  28800      /* 8 小时 */

struct Config {
    /* [Schemes] */
    GUID  guidA;
    BOOL  hasA;
    GUID  guidB;
    BOOL  hasB;

    /* [Labels] */
    COLORREF  colorA;
    COLORREF  colorB;
    COLORREF  fgColor;
    COLORREF  neutralColor;

    /* [Idle] */
    BOOL  idleEnabled;
    int   idleThresholdSec;
    int   idleTarget;
    GUID  guidCustom;
    BOOL  hasCustom;
    BOOL  revertOnResume;
    int   cooldownSec;

    /* [General] */
    BOOL  autoStart;
    BOOL  showBalloon;
    BOOL  firstRunDone;
    int   logLevel;
};

void ConfigDefaults(Config* c);

/* 读取成功返回 TRUE（文件不存在返回 FALSE，c 保持默认值） */
BOOL ConfigLoad(const WCHAR* path, Config* c);

/* 保存成功返回 TRUE */
BOOL ConfigSave(const WCHAR* path, const Config* c);

/* 解析配置文件应存放的位置：
   exe 同目录优先，目录不可写则回退 %APPDATA%\PowerToggle */
void ConfigResolvePath(WCHAR* fileOut, int cchFile, WCHAR* dirOut, int cchDir);

/* 颜色工具 */
COLORREF ColorFromHex(const WCHAR* text, COLORREF def);
void     ColorToHex(COLORREF c, WCHAR* out, int cchOut);

#endif /* PT_CONFIG_H */
