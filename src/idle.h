/* idle.h —— 空闲检测与自动切换状态机 */
#ifndef PT_IDLE_H
#define PT_IDLE_H

#include "ptdef.h"
#include <windows.h>
#include "config.h"

#define IDLE_NONE    0    /* 无动作 */
#define IDLE_APPLY   1    /* 切到空闲目标方案 */
#define IDLE_REVERT  2    /* 切回空闲前方案 */

struct IdleEngine {
    ULONGLONG manualTick;     /* 上次手动切换时刻（GetTickCount64） */
    ULONGLONG cooldownUntil;  /* 冷却到期时刻 */
    BOOL      autoApplied;    /* 当前是否处于"已自动切换"状态 */
    BOOL      hasSnapshot;    /* 是否记录了快照 */
    GUID      snapshot;       /* 空闲触发前的方案 */
    GUID      applied;        /* 工具应用的目标方案 */

    /* 最近一次空闲切换的源/目标与空闲时长，供调用方在切换成功后写专用日志 */
    GUID      logFrom;
    GUID      logTo;
    DWORD     logIdleSec;
};

void  IdleInit(IdleEngine* e);
void  IdleOnManualSwitch(IdleEngine* e);
void  IdleOnConfigChanged(IdleEngine* e);

/* 系统空闲毫秒数（无符号减法，天然处理 49.7 天回绕） */
DWORD IdleGetSystemMs(void);

/* 推进状态机。
   now        : 当前时刻（GetTickCount64），由调用方注入以便测试
   sysIdle    : 系统空闲毫秒数，由调用方注入以便测试
   targetGuid/targetValid: 本次应切往的空闲目标方案
   current/hasCurrent    : 系统当前激活方案
   outGuid               : 返回需要切换到的方案（IDLE_APPLY / IDLE_REVERT 时有效） */
int IdleStep(IdleEngine* e, const Config* c,
             ULONGLONG now, DWORD sysIdle,
             const GUID* targetGuid, BOOL targetValid,
             const GUID* current, BOOL hasCurrent,
             GUID* outGuid);

#endif /* PT_IDLE_H */
