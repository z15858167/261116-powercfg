/* ptdef.h —— 全局编译期定义，必须最先包含 */
#ifndef PT_DEF_H
#define PT_DEF_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601   /* Windows 7+，PowerEnumerate 等需要 */
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define APP_NAME      L"PowerToggle"
#define APP_TITLE     L"PowerToggle 设置"
#define APP_VERSION   L"1.0.0"

#endif /* PT_DEF_H */
