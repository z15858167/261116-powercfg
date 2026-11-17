/* util.h —— 字符串/路径/日志/格式化等基础设施 */
#ifndef PT_UTIL_H
#define PT_UTIL_H

#include "ptdef.h"
#include <windows.h>

/* ---------- 轻量字符串构建器（不依赖 CRT） ---------- */
struct Sb {
    WCHAR* buf;
    int    cch;
    int    len;
};

void SbInit(Sb* sb, WCHAR* buf, int cch);
void SbAppend(Sb* sb, const WCHAR* s);
void SbAppendF(Sb* sb, const WCHAR* fmt, ...);
void SbClear(Sb* sb);

/* ---------- 字符串 ---------- */
void StrCopy(WCHAR* dst, int cchDst, const WCHAR* src);
BOOL StrEqualI(const WCHAR* a, const WCHAR* b);
BOOL StrIsEmpty(const WCHAR* s);
void StrTrim(WCHAR* s);
/* 取前 n 个字符（按字符而非字节），用于中性态显示 */
void StrFirstChars(const WCHAR* src, WCHAR* dst, int cchDst, int n);

/* 组合显示名：detail 非空时输出 "base（detail）"，否则输出 base */
void StrFormatLabel(WCHAR* out, int cchOut, const WCHAR* base, const WCHAR* detail);

/* ---------- 路径与文件 ---------- */
void PathGetExeDir(WCHAR* out, int cchOut);
void PathGetExePath(WCHAR* out, int cchOut);
BOOL PathCombine(WCHAR* out, int cchOut, const WCHAR* dir, const WCHAR* name);
BOOL FileExists(const WCHAR* path);
BOOL DirEnsure(const WCHAR* path);
BOOL DirIsWritable(const WCHAR* dir);

/* ---------- 日志 ---------- */
void LogSetLevel(int level);
void LogSetPath(const WCHAR* path);
void LogFmt(int level, const WCHAR* fmt, ...);

/* 空闲切换专用日志：独立文件、始终写入、不受 LogLevel 影响、只追加不覆盖 */
void LogSetIdlePath(const WCHAR* path);
void IdleLog(const WCHAR* fmt, ...);

/* ---------- 错误码转文本 ---------- */
void ErrText(DWORD code, WCHAR* out, int cchOut);

#endif /* PT_UTIL_H */
