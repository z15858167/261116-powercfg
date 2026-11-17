/* idle.cpp */
#include "idle.h"
#include "power.h"
#include "util.h"

#define RESUME_IDLE_MS  1000   /* 系统空闲小于此值视为"刚刚恢复操作" */

void IdleInit(IdleEngine* e)
{
    ZeroMemory(e, sizeof(IdleEngine));
    e->manualTick = GetTickCount64();
}

void IdleOnManualSwitch(IdleEngine* e)
{
    e->manualTick  = GetTickCount64();
    e->autoApplied = FALSE;
    e->hasSnapshot = FALSE;
    ZeroMemory(&e->applied, sizeof(GUID));
}

void IdleOnConfigChanged(IdleEngine* e)
{
    e->autoApplied = FALSE;
    e->hasSnapshot = FALSE;
    ZeroMemory(&e->applied, sizeof(GUID));
}

DWORD IdleGetSystemMs(void)
{
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(lii);
    lii.dwTime = 0;
    if (!GetLastInputInfo(&lii)) return 0;
    return GetTickCount() - lii.dwTime;
}

int IdleStep(IdleEngine* e, const Config* c,
             ULONGLONG now, DWORD sysIdle,
             const GUID* targetGuid, BOOL targetValid,
             const GUID* current, BOOL hasCurrent,
             GUID* outGuid)
{
    DWORD     effIdle;
    ULONGLONG sinceManual;
    ULONGLONG needMs;
    int       cooldown = 0;

    if (!e || !c || !outGuid) return IDLE_NONE;

    sinceManual = (now > e->manualTick) ? (now - e->manualTick) : 0;

    /* 有效空闲 = min(系统空闲, 距上次手动切换的时长) */
    effIdle = sysIdle;
    if ((ULONGLONG)effIdle > sinceManual) {
        effIdle = (sinceManual > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (DWORD)sinceManual;
    }

    if (!c->idleEnabled) {
        e->autoApplied = FALSE;
        e->hasSnapshot = FALSE;
        return IDLE_NONE;
    }

    cooldown = (c->cooldownSec > 0) ? c->cooldownSec : 0;

    if (e->autoApplied) {
        if (sysIdle < RESUME_IDLE_MS) {
            BOOL canRevert = (c->revertOnResume && e->hasSnapshot) ? TRUE : FALSE;
            BOOL matches   = (hasCurrent && current &&
                              GuidEqual(current, &e->applied)) ? TRUE : FALSE;
            GUID snap      = e->snapshot;

            e->autoApplied = FALSE;
            e->hasSnapshot = FALSE;

            if (canRevert && matches) {
                *outGuid = snap;
                e->cooldownUntil = now + (ULONGLONG)cooldown * 1000ULL;
                e->logFrom       = e->applied;   /* 自动切换应用的目标，即回切前方案 */
                e->logTo         = snap;
                e->logIdleSec    = 0;
                LogFmt(2, L"恢复操作，切回空闲前方案");
                return IDLE_REVERT;
            }
            return IDLE_NONE;
        }
        return IDLE_NONE;
    }

    needMs = (ULONGLONG)(c->idleThresholdSec > 0 ? c->idleThresholdSec : IDLE_MIN_SEC) * 1000ULL;
    if ((ULONGLONG)effIdle < needMs) return IDLE_NONE;
    if (now < e->cooldownUntil) return IDLE_NONE;
    if (!targetValid || !targetGuid) return IDLE_NONE;

    /* 已经是目标方案则不切换（避免无意义的 PowerSetActiveScheme 调用） */
    if (hasCurrent && current && GuidEqual(current, targetGuid)) return IDLE_NONE;

    *outGuid         = *targetGuid;
    e->applied       = *targetGuid;
    e->autoApplied   = TRUE;
    e->hasSnapshot   = (hasCurrent && current) ? TRUE : FALSE;
    if (e->hasSnapshot) e->snapshot = *current;
    e->cooldownUntil = now + (ULONGLONG)cooldown * 1000ULL;

    if (hasCurrent && current) e->logFrom = *current;
    else ZeroMemory(&e->logFrom, sizeof(GUID));
    e->logTo      = *targetGuid;
    e->logIdleSec = (DWORD)(effIdle / 1000);

    LogFmt(2, L"空闲 %u 秒达到阈值，自动切换", (unsigned)(effIdle / 1000));
    return IDLE_APPLY;
}
