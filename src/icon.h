/* icon.h —— 运行时用 GDI 动态生成托盘图标 */
#ifndef PT_ICON_H
#define PT_ICON_H

#include "ptdef.h"
#include <windows.h>

/* 生成一枚图标：圆角方块底色 + 闪电（前景色），靠形状和底色区分状态。
   bg    : 底色（方案 A/B 的自定义颜色，或中性/异常灰）
   fg    : 闪电颜色
   size  : 边长像素（16/20/24/32）
   返回的 HICON 需由调用方 DestroyIcon。失败返回 NULL。 */
HICON IconBuild(COLORREF bg, COLORREF fg, int size);

/* 取当前 DPI 下推荐的小图标边长 */
int IconGetSmallSize(HWND hwnd);

#endif /* PT_ICON_H */
