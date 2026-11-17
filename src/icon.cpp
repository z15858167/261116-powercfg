/* icon.cpp —— 32bpp DIB 手绘"圆角方块 + 白色闪电"，生成 HICON */
#include "icon.h"
#include <math.h>

struct Dib {
    HBITMAP hbm;
    HBITMAP hOld;
    HDC     hdc;
    void*   bits;
    int     w;
    int     h;
};

static BOOL DibCreate(Dib* d, int w, int h)
{
    BITMAPINFO bi;
    HDC screen;
    void* bits = NULL;

    ZeroMemory(d, sizeof(Dib));
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          /* 负数 = 自上而下 */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    if (!screen) return FALSE;

    d->hdc = CreateCompatibleDC(screen);
    d->hbm = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);

    if (!d->hdc || !d->hbm || !bits) {
        if (d->hbm) DeleteObject(d->hbm);
        if (d->hdc) DeleteDC(d->hdc);
        ZeroMemory(d, sizeof(Dib));
        return FALSE;
    }

    d->bits = bits;
    d->w = w;
    d->h = h;
    d->hOld = (HBITMAP)SelectObject(d->hdc, d->hbm);
    ZeroMemory(bits, (size_t)w * h * 4);
    return TRUE;
}

static void DibDestroy(Dib* d)
{
    if (d->hdc) {
        if (d->hOld) SelectObject(d->hdc, d->hOld);
        DeleteDC(d->hdc);
    }
    if (d->hbm) DeleteObject(d->hbm);
    ZeroMemory(d, sizeof(Dib));
}

/* 白色闪电多边形（归一化坐标，与 res/make_icon.ps1 保持一致） */
static const float BOLT_NORM[6][2] = {
    {0.52f, 0.06f}, {0.20f, 0.55f}, {0.42f, 0.55f},
    {0.36f, 0.94f}, {0.74f, 0.42f}, {0.51f, 0.42f}
};

static BOOL PointInPoly(const float (*poly)[2], int n, float x, float y)
{
    int i, j;
    BOOL inside = FALSE;

    j = n - 1;
    for (i = 0; i < n; j = i++) {
        float xi = poly[i][0], yi = poly[i][1];
        float xj = poly[j][0], yj = poly[j][1];
        if ((yi > y) != (yj > y) &&
            x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

/* 4x4 超采样得到闪电在像素内的覆盖率（抗锯齿） */
static float BoltCoverage(const float (*bolt)[2], float px, float py)
{
    int sx, sy, hit = 0;

    for (sy = 0; sy < 4; sy++) {
        for (sx = 0; sx < 4; sx++) {
            if (PointInPoly(bolt, 6,
                            px + (sx + 0.5f) * 0.25f,
                            py + (sy + 0.5f) * 0.25f))
                hit++;
        }
    }
    return hit / 16.0f;
}

HICON IconBuild(COLORREF bg, COLORREF fg, int size)
{
    Dib outDib;
    HBITMAP hMask = NULL;
    HICON hIcon = NULL;
    ICONINFO ii;
    int x, y, i;
    float half, radius, inner;
    float bolt[6][2];
    int br, bgn, bb;
    int fr, fgn, fb;

    if (size < 8) size = 16;
    if (size > 128) size = 128;

    if (!DibCreate(&outDib, size, size)) return NULL;

    for (i = 0; i < 6; i++) {
        bolt[i][0] = BOLT_NORM[i][0] * size;
        bolt[i][1] = BOLT_NORM[i][1] * size;
    }

    half   = size * 0.5f;
    radius = size * 0.22f;
    if (radius < 1.5f) radius = 1.5f;
    inner  = half - 0.5f - radius;   /* 圆角矩形内缩后的半宽 */

    br  = GetRValue(bg); bgn = GetGValue(bg); bb = GetBValue(bg);
    fr  = GetRValue(fg); fgn = GetGValue(fg); fb = GetBValue(fg);

    {
        BYTE* op = (BYTE*)outDib.bits;

        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                float px = (float)x + 0.5f - half;
                float py = (float)y + 0.5f - half;
                float qx = fabsf(px) - inner;
                float qy = fabsf(py) - inner;
                float ax = qx > 0 ? qx : 0;
                float ay = qy > 0 ? qy : 0;
                float dist = sqrtf(ax * ax + ay * ay);
                float m = qx > qy ? qx : qy;
                float cov;
                float bt;
                int idx = (y * size + x) * 4;
                int r, g, b, a;

                if (m < 0) dist += m;
                dist -= radius;

                cov = 0.5f - dist;
                if (cov <= 0.0f) cov = 0.0f;
                if (cov >= 1.0f) cov = 1.0f;

                if (cov <= 0.0f) {
                    op[idx + 0] = 0;
                    op[idx + 1] = 0;
                    op[idx + 2] = 0;
                    op[idx + 3] = 0;
                    continue;
                }

                /* 闪电覆盖率：底色 -> 前景色（默认白色）渐变 */
                bt = BoltCoverage(bolt, (float)x, (float)y);
                r = br + (int)((fr - br) * bt);
                g = bgn + (int)((fgn - bgn) * bt);
                b = bb + (int)((fb - bb) * bt);

                a = (int)(cov * 255.0f + 0.5f);
                if (a > 255) a = 255;

                op[idx + 0] = (BYTE)b;
                op[idx + 1] = (BYTE)g;
                op[idx + 2] = (BYTE)r;
                op[idx + 3] = (BYTE)a;
            }
        }
    }

    /* 单色掩码：全 0 表示"全部不透明，按 alpha 混合" */
    hMask = CreateBitmap(size, size, 1, 1, NULL);
    if (hMask) {
        int stride = ((size + 31) / 32) * 4;
        BYTE* zeros = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       (SIZE_T)stride * size);
        if (zeros) {
            SetBitmapBits(hMask, (DWORD)(stride * size), zeros);
            HeapFree(GetProcessHeap(), 0, zeros);
        }
    }

    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon    = TRUE;
    ii.hbmColor = outDib.hbm;
    ii.hbmMask  = hMask;

    if (hMask) hIcon = CreateIconIndirect(&ii);

    if (hMask) DeleteObject(hMask);
    DibDestroy(&outDib);

    return hIcon;
}

int IconGetSmallSize(HWND hwnd)
{
    HDC dc;
    int px;
    (void)hwnd;
    dc = GetDC(NULL);
    if (!dc) return 16;
    px = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    /* 96dpi → 16, 120dpi → 20, 144dpi → 24, 192dpi → 32 */
    if (px >= 192) return 32;
    if (px >= 144) return 24;
    if (px >= 120) return 20;
    return 16;
}
