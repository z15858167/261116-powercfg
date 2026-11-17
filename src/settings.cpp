/* settings.cpp —— 设置对话框 */
#include "settings.h"
#include "resource.h"
#include "util.h"
#include "power.h"
#include "icon.h"
#include "autostart.h"
#include <commctrl.h>
#include <commdlg.h>

struct SettingsCtx {
    Config      cfg;
    SchemeList  schemes;
    BOOL        firstRun;
    BOOL        saved;
    COLORREF    swA;
    COLORREF    swB;
    HBRUSH      brA;
    HBRUSH      brB;
    WCHAR       cfgFile[MAX_PATH];
};

static SettingsCtx* g_s = NULL;

/* ============ 工具函数 ============ */

static void FillSchemeCombo(HWND hDlg, int id, const SchemeList* list,
                            const GUID* sel, BOOL hasSel)
{
    HWND  h = GetDlgItem(hDlg, id);
    int   i;
    int   selIdx = -1;

    if (!h) return;
    SendMessageW(h, CB_RESETCONTENT, 0, 0);

    /* 用 CB_INSERTSTRING 指定下标，避免同名方案被 CB_ADDSTRING 去重后下标错位 */
    for (i = 0; i < list->count; i++) {
        int idx = (int)SendMessageW(h, CB_INSERTSTRING, (WPARAM)i, (LPARAM)list->items[i].name);
        if (idx < 0) continue;
        SendMessageW(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)i);
        if (hasSel && GuidEqual(&list->items[i].guid, sel)) selIdx = idx;
    }

    if (hasSel && selIdx < 0) {
        int idx = (int)SendMessageW(h, CB_INSERTSTRING, (WPARAM)list->count,
                                    (LPARAM)L"（原方案已失效，请重新选择）");
        if (idx >= 0) {
            SendMessageW(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)-1);
            selIdx = idx;
        }
    }

    SendMessageW(h, CB_SETCURSEL, (WPARAM)(selIdx >= 0 ? selIdx : 0), 0);
}

/* 读回下拉框选中的方案，成功返回 TRUE */
static BOOL ReadComboScheme(HWND hDlg, int id, const SchemeList* list, GUID* out)
{
    HWND h = GetDlgItem(hDlg, id);
    int  sel, data;

    if (!h || !out) return FALSE;
    sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (sel < 0) return FALSE;
    data = (int)SendMessageW(h, CB_GETITEMDATA, (WPARAM)sel, 0);
    if (data < 0 || data >= list->count) return FALSE;
    *out = list->items[data].guid;
    return TRUE;
}

/* 空闲目标下拉显示"方案 A（节能）"这样的实际方案名，避免用户不知道 A 是什么 */
static void UpdateTargetLabels(HWND hDlg)
{
    HWND h = GetDlgItem(hDlg, IDC_CMB_TARGET);
    GUID ga, gb;
    SchemeInfo sa, sb;
    WCHAR label[96];
    int  sel;

    if (!h) return;

    sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    SendMessageW(h, CB_RESETCONTENT, 0, 0);

    if (ReadComboScheme(hDlg, IDC_CMB_A, &g_s->schemes, &ga) &&
        PowerFindByGuid(&g_s->schemes, &ga, &sa)) {
        StrFormatLabel(label, 96, L"方案 A", sa.name);
    } else {
        StrFormatLabel(label, 96, L"方案 A", L"未选择");
    }
    SendMessageW(h, CB_INSERTSTRING, 0, (LPARAM)label);

    if (ReadComboScheme(hDlg, IDC_CMB_B, &g_s->schemes, &gb) &&
        PowerFindByGuid(&g_s->schemes, &gb, &sb)) {
        StrFormatLabel(label, 96, L"方案 B", sb.name);
    } else {
        StrFormatLabel(label, 96, L"方案 B", L"未选择");
    }
    SendMessageW(h, CB_INSERTSTRING, 1, (LPARAM)label);

    SendMessageW(h, CB_INSERTSTRING, 2, (LPARAM)L"自定义方案");

    if (sel < 0 || sel > TARGET_CUSTOM) sel = 0;
    SendMessageW(h, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void RebuildBrushes(HWND hDlg)
{
    if (g_s->brA) { DeleteObject(g_s->brA); g_s->brA = NULL; }
    if (g_s->brB) { DeleteObject(g_s->brB); g_s->brB = NULL; }
    g_s->brA = CreateSolidBrush(g_s->swA);
    g_s->brB = CreateSolidBrush(g_s->swB);
    InvalidateRect(GetDlgItem(hDlg, IDC_SW_A), NULL, TRUE);
    InvalidateRect(GetDlgItem(hDlg, IDC_SW_B), NULL, TRUE);
}

static int GetTargetIndex(HWND hDlg)
{
    int sel = (int)SendMessageW(GetDlgItem(hDlg, IDC_CMB_TARGET), CB_GETCURSEL, 0, 0);
    return (sel < 0) ? 0 : sel;
}

/* 自定义方案下拉：仅在"自定义方案"被选中时启用，否则清空显示 */
static void SyncCustomCombo(HWND hDlg, BOOL enable)
{
    HWND h = GetDlgItem(hDlg, IDC_CMB_CUSTOM);

    if (!h) return;
    EnableWindow(h, enable);

    if (!enable) {
        SendMessageW(h, CB_SETCURSEL, (WPARAM)(-1), 0);
    } else if ((int)SendMessageW(h, CB_GETCURSEL, 0, 0) < 0) {
        FillSchemeCombo(hDlg, IDC_CMB_CUSTOM, &g_s->schemes,
                        &g_s->cfg.guidCustom, g_s->cfg.hasCustom);
    }
}

static void UpdateEnabledState(HWND hDlg)
{
    BOOL on = (IsDlgButtonChecked(hDlg, IDC_CHK_IDLE) == BST_CHECKED) ? TRUE : FALSE;
    BOOL custom = on && (GetTargetIndex(hDlg) == TARGET_CUSTOM);

    EnableWindow(GetDlgItem(hDlg, IDC_LBL_AFTER), on);
    EnableWindow(GetDlgItem(hDlg, IDC_EDT_MIN), on);
    EnableWindow(GetDlgItem(hDlg, IDC_SPN_MIN), on);
    EnableWindow(GetDlgItem(hDlg, IDC_LBL_TO), on);
    EnableWindow(GetDlgItem(hDlg, IDC_CMB_TARGET), on);
    EnableWindow(GetDlgItem(hDlg, IDC_CHK_REVERT), on);
    SyncCustomCombo(hDlg, custom);
}

static void InvalidatePreviews(HWND hDlg)
{
    InvalidateRect(GetDlgItem(hDlg, IDC_PREV_A), NULL, TRUE);
    InvalidateRect(GetDlgItem(hDlg, IDC_PREV_B), NULL, TRUE);
}

/* ============ 初始化 ============ */

static void OnInit(HWND hDlg)
{
    HWND hEdit;

    SetWindowTextW(hDlg, g_s->firstRun ? L"PowerToggle 首次配置" : APP_TITLE);

    /* 标题栏 / 任务栏图标（windres 的 DIALOGEX 不支持 ICON 语句，只能运行时设置） */
    {
        HICON appIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));
        if (appIcon) {
            SendMessageW(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)appIcon);
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)appIcon);
        }
    }
    ShowWindow(GetDlgItem(hDlg, IDC_HINT), g_s->firstRun ? SW_SHOW : SW_HIDE);

    g_s->swA = g_s->cfg.colorA;
    g_s->swB = g_s->cfg.colorB;

    FillSchemeCombo(hDlg, IDC_CMB_A, &g_s->schemes, &g_s->cfg.guidA, g_s->cfg.hasA);
    FillSchemeCombo(hDlg, IDC_CMB_B, &g_s->schemes, &g_s->cfg.guidB, g_s->cfg.hasB);

    CheckDlgButton(hDlg, IDC_CHK_IDLE,
                   g_s->cfg.idleEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHK_REVERT,
                   g_s->cfg.revertOnResume ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHK_AUTOSTART,
                   g_s->cfg.autoStart ? BST_CHECKED : BST_UNCHECKED);

    /* 空闲目标下拉：先设初值，再由 UpdateTargetLabels 用实际方案名重建 */
    SendMessageW(GetDlgItem(hDlg, IDC_CMB_TARGET), CB_SETCURSEL,
                 (WPARAM)g_s->cfg.idleTarget, 0);
    UpdateTargetLabels(hDlg);

    FillSchemeCombo(hDlg, IDC_CMB_CUSTOM, &g_s->schemes,
                    &g_s->cfg.guidCustom, g_s->cfg.hasCustom);

    /* 分钟数 */
    {
        int minutes = (g_s->cfg.idleThresholdSec + 59) / 60;
        if (minutes < 1) minutes = 1;
        SetDlgItemInt(hDlg, IDC_EDT_MIN, (UINT)minutes, FALSE);
    }

    /* 微调按钮 */
    hEdit = GetDlgItem(hDlg, IDC_EDT_MIN);
    if (hEdit) {
        HWND hSpin;
        RECT rc;
        GetWindowRect(hEdit, &rc);
        MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);
        hSpin = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                                WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS |
                                UDS_SETBUDDYINT | UDS_NOTHOUSANDS,
                                rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                                hDlg, (HMENU)(INT_PTR)IDC_SPN_MIN,
                                GetModuleHandleW(NULL), NULL);
        if (hSpin) {
            SendMessageW(hSpin, UDM_SETBUDDY, (WPARAM)hEdit, 0);
            SendMessageW(hSpin, UDM_SETRANGE32, 1, 480);
            SendMessageW(hSpin, UDM_SETPOS32, 0, (LPARAM)((g_s->cfg.idleThresholdSec + 59) / 60));
        }
    }

    RebuildBrushes(hDlg);
    UpdateEnabledState(hDlg);
    InvalidatePreviews(hDlg);
}

/* ============ 保存校验 ============ */

static BOOL ValidateAndStore(HWND hDlg)
{
    GUID  ga, gb, gc;
    BOOL  okA, okB;
    UINT  minutes;
    BOOL  trans = FALSE;

    okA = ReadComboScheme(hDlg, IDC_CMB_A, &g_s->schemes, &ga);
    okB = ReadComboScheme(hDlg, IDC_CMB_B, &g_s->schemes, &gb);

    if (!okA || !okB) {
        MessageBoxW(hDlg, L"请为方案 A 和方案 B 各选择一个有效的电源方案。",
                    APP_NAME, MB_ICONWARNING | MB_OK);
        return FALSE;
    }
    if (GuidEqual(&ga, &gb)) {
        MessageBoxW(hDlg, L"方案 A 与方案 B 不能是同一个方案。",
                    APP_NAME, MB_ICONWARNING | MB_OK);
        return FALSE;
    }

    minutes = GetDlgItemInt(hDlg, IDC_EDT_MIN, &trans, FALSE);
    if (!trans || minutes < 1 || minutes > 480) {
        MessageBoxW(hDlg, L"空闲时长需在 1 ~ 480 分钟之间。", APP_NAME, MB_ICONWARNING | MB_OK);
        return FALSE;
    }

    {
        int target = GetTargetIndex(hDlg);
        if (target == TARGET_CUSTOM) {
            if (!ReadComboScheme(hDlg, IDC_CMB_CUSTOM, &g_s->schemes, &gc)) {
                MessageBoxW(hDlg, L"请选择空闲时切换到的自定义方案。", APP_NAME,
                            MB_ICONWARNING | MB_OK);
                return FALSE;
            }
            g_s->cfg.guidCustom = gc;
            g_s->cfg.hasCustom = TRUE;
        }
        g_s->cfg.idleTarget = target;
    }

    g_s->cfg.guidA = ga;
    g_s->cfg.hasA = TRUE;
    g_s->cfg.guidB = gb;
    g_s->cfg.hasB = TRUE;
    g_s->cfg.colorA = g_s->swA;
    g_s->cfg.colorB = g_s->swB;
    g_s->cfg.idleEnabled = (IsDlgButtonChecked(hDlg, IDC_CHK_IDLE) == BST_CHECKED);
    g_s->cfg.revertOnResume = (IsDlgButtonChecked(hDlg, IDC_CHK_REVERT) == BST_CHECKED);
    g_s->cfg.autoStart = (IsDlgButtonChecked(hDlg, IDC_CHK_AUTOSTART) == BST_CHECKED);
    g_s->cfg.idleThresholdSec = (int)minutes * 60;
    g_s->cfg.firstRunDone = TRUE;

    return TRUE;
}

/* ============ 对话框过程 ============ */

static void OnColorPick(HWND hDlg, BOOL isA)
{
    CHOOSECOLORW cc;
    COLORREF cust[16];
    COLORREF init = isA ? g_s->swA : g_s->swB;

    ZeroMemory(&cc, sizeof(cc));
    ZeroMemory(cust, sizeof(cust));
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = hDlg;
    cc.rgbResult    = init;
    cc.lpCustColors = cust;
    cc.Flags        = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        if (isA) g_s->swA = cc.rgbResult;
        else     g_s->swB = cc.rgbResult;
        RebuildBrushes(hDlg);
        InvalidatePreviews(hDlg);
    }
}

static void OnDrawItem(HWND hDlg, LPDRAWITEMSTRUCT dis)
{
    BOOL isA;
    COLORREF bg;
    HICON ic;
    RECT rc;
    HBRUSH face;

    (void)hDlg;
    if (dis->CtlID != IDC_PREV_A && dis->CtlID != IDC_PREV_B) return;
    isA = (dis->CtlID == IDC_PREV_A);

    bg = isA ? g_s->swA : g_s->swB;

    face = GetSysColorBrush(COLOR_BTNFACE);
    FillRect(dis->hDC, &dis->rcItem, face);

    rc = dis->rcItem;
    ic = IconBuild(bg, g_s->cfg.fgColor, 32);
    if (ic) {
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int x = rc.left + (w - 32) / 2;
        int y = rc.top + (h - 32) / 2;
        if (w < 32) x = rc.left;
        if (h < 32) y = rc.top;
        DrawIconEx(dis->hDC, x, y, ic, 32, 32, 0, NULL, DI_NORMAL);
        DestroyIcon(ic);
    }
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        g_s = (SettingsCtx*)lParam;
        OnInit(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (ValidateAndStore(hDlg)) {
                g_s->saved = TRUE;
                EndDialog(hDlg, TRUE);
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, FALSE);
            return TRUE;

        case IDC_SW_A:
            if (HIWORD(wParam) == STN_CLICKED) { OnColorPick(hDlg, TRUE); return TRUE; }
            break;

        case IDC_SW_B:
            if (HIWORD(wParam) == STN_CLICKED) { OnColorPick(hDlg, FALSE); return TRUE; }
            break;

        case IDC_CHK_IDLE:
            UpdateEnabledState(hDlg);
            return TRUE;

        case IDC_CMB_A:
        case IDC_CMB_B:
            if (HIWORD(wParam) == CBN_SELCHANGE) { UpdateTargetLabels(hDlg); return TRUE; }
            break;

        case IDC_CMB_TARGET:
            if (HIWORD(wParam) == CBN_SELCHANGE) { UpdateEnabledState(hDlg); return TRUE; }
            break;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HWND ctl = (HWND)lParam;
        if (ctl == GetDlgItem(hDlg, IDC_SW_A)) {
            SetBkColor((HDC)wParam, g_s->swA);
            return (INT_PTR)g_s->brA;
        }
        if (ctl == GetDlgItem(hDlg, IDC_SW_B)) {
            SetBkColor((HDC)wParam, g_s->swB);
            return (INT_PTR)g_s->brB;
        }
        break;
    }

    case WM_DRAWITEM:
        OnDrawItem(hDlg, (LPDRAWITEMSTRUCT)lParam);
        return TRUE;

    case WM_CLOSE:
        EndDialog(hDlg, FALSE);
        return TRUE;
    }
    return FALSE;
}

BOOL SettingsOpen(HINSTANCE hInst, HWND owner, Config* cfg,
                  const WCHAR* cfgFile, BOOL firstRun)
{
    SettingsCtx ctx;
    INT_PTR     r;

    if (!cfg || !hInst) return FALSE;

    ZeroMemory(&ctx, sizeof(ctx));
    ctx.cfg = *cfg;
    ctx.firstRun = firstRun;
    StrCopy(ctx.cfgFile, MAX_PATH, cfgFile);
    PowerEnumSchemes(&ctx.schemes);

    if (ctx.schemes.count == 0) {
        MessageBoxW(owner, L"未能读取到系统电源方案，请稍后重试。",
                    APP_NAME, MB_ICONERROR | MB_OK);
        return FALSE;
    }

    r = DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS), owner,
                        DlgProc, (LPARAM)&ctx);

    if (g_s && g_s->brA) { DeleteObject(g_s->brA); g_s->brA = NULL; }
    if (g_s && g_s->brB) { DeleteObject(g_s->brB); g_s->brB = NULL; }
    g_s = NULL;

    if (r == TRUE && ctx.saved) {
        *cfg = ctx.cfg;
        if (cfgFile && cfgFile[0]) ConfigSave(cfgFile, cfg);
        return TRUE;
    }
    return FALSE;
}
