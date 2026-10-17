#ifndef ACCENT_POLICY_DEFINED
#define ACCENT_POLICY_DEFINED
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include "theme.h"

// Simple 32x32 noise pattern for acrylic simulation
static const unsigned char kAcrylicNoise32[32][32] = {
    { 98, 12, 56, 34, 78, 90, 23, 67, 45, 89, 12, 34, 56, 78, 90, 23, 67, 45, 89, 12, 34, 56, 78, 90, 23, 67, 45, 89, 12, 34, 56, 78 }
};
#endif
#include "big_menu_api.h"
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <math.h>
#include <mmsystem.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Winmm.lib")

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif

#ifndef WM_POINTERDOWN
#define WM_POINTERDOWN 0x0246
#endif

#ifndef WCA_ACCENT_POLICY
#define WCA_ACCENT_POLICY 19
#endif

#define BIG_MENU_ANIM_TIMER_ID 1
#define BIG_MENU_ANIM_DURATION_MS 220

typedef enum DWM_WINDOW_CORNER_PREFERENCE_LOCAL {
    DWMWCP_DEFAULT_LOCAL = 0,
    DWMWCP_DONOTROUND_LOCAL = 1,
    DWMWCP_ROUND_LOCAL = 2,
    DWMWCP_ROUNDSMALL_LOCAL = 3
} DWM_WINDOW_CORNER_PREFERENCE_LOCAL;


typedef LONG (WINAPI* RTLGETVERSIONPROC)(PRTL_OSVERSIONINFOW);

typedef enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
} ACCENT_STATE;

typedef struct ACCENT_POLICY {
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
} ACCENT_POLICY;

typedef struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL (WINAPI* SETWINDOWCOMPOSITIONATTRIBUTE)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

typedef struct BIG_MENU_STATE {
    const WMM_BIG_MENU_SHOW_PARAMS* params;
    WMM_BIG_MENU_RESULT* result;
    HWND hwnd;
    HFONT font;
    int dpi;
    int width;
    int totalHeight;
    int itemHeight;
    int separatorHeight;
    int iconSize;
    int leftPad;
    int iconGap;
    int rightPad;
    int submenuPad;
    int selectionRadius;
    int hoverIndex;
    int keyboardIndex;
    int submenuIndex;
    int lastSubmenuCloseIndex;
    DWORD lastSubmenuCloseTick;
    BOOL running;
    BOOL inSubmenu;
    BOOL mouseTracked;
    BOOL closeAfterSubmenu;
    BOOL animating;
    BOOL useBackdrop;
    BOOL highResTimerActive;
    BOOL useQpc;
    DWORD animationStartTick;
    LARGE_INTEGER animationStartQpc;
    LARGE_INTEGER animationQpcFreq;
    POINT animationStart;
    POINT animationTarget;
    UINT animationDurationMs;
    UINT animationDirection;
} BIG_MENU_STATE;

static const WCHAR kBigMenuClassName[] = L"WinMacMenu.BigMenuWindow";
static HHOOK g_bigMenuMsgHook = NULL;
static BIG_MENU_STATE* g_bigMenuHookState = NULL;
static int g_bigMenuPendingHoverIndex = -1;
static int g_bigMenuHoverIndexFromHook = -1;
static HHOOK g_bigMenuCbtHook = NULL;
static BOOL g_bigMenuCbtDarkMode = FALSE;

static LRESULT CALLBACK big_menu_msgfilter_proc(int code, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK big_menu_cbt_proc(int code, WPARAM wParam, LPARAM lParam);

static BOOL big_menu_is_windows_11_or_greater(void) {
    HMODULE ntdll;
    RTLGETVERSIONPROC rtlGetVersion;
    RTL_OSVERSIONINFOW versionInfo;

    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return FALSE;

    rtlGetVersion = (RTLGETVERSIONPROC)GetProcAddress(ntdll, "RtlGetVersion");
    if (!rtlGetVersion) return FALSE;

    ZeroMemory(&versionInfo, sizeof(versionInfo));
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
    if (rtlGetVersion(&versionInfo) != 0) return FALSE;

    if (versionInfo.dwMajorVersion > 10) return TRUE;
    return (versionInfo.dwMajorVersion == 10 && versionInfo.dwBuildNumber >= 22000);
}

static BOOL big_menu_read_reg_dword(HKEY root, LPCWSTR subkey, LPCWSTR value, DWORD* out) {
    HKEY key;
    DWORD type = 0;
    DWORD cb = sizeof(DWORD);

    if (!out) return FALSE;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return FALSE;
    if (RegQueryValueExW(key, value, NULL, &type, (LPBYTE)out, &cb) != ERROR_SUCCESS || type != REG_DWORD) {
        RegCloseKey(key);
        return FALSE;
    }
    RegCloseKey(key);
    return TRUE;
}

static BOOL big_menu_is_system_dark_mode(void) {
    DWORD appsUseLight = 1;
    if (big_menu_read_reg_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", &appsUseLight)) {
        return appsUseLight == 0;
    }
    return FALSE;
}

static COLORREF big_menu_blend_colors(COLORREF base, COLORREF top, int alpha) {
    int inv = 255 - alpha;
    int r = (GetRValue(base) * inv + GetRValue(top) * alpha) / 255;
    int g = (GetGValue(base) * inv + GetGValue(top) * alpha) / 255;
    int b = (GetBValue(base) * inv + GetBValue(top) * alpha) / 255;
    return RGB(r, g, b);
}

// Use the same logic as the submenu for background color
#include "theme.h"
static COLORREF big_menu_get_background_color(BOOL darkMode) {
    if (darkMode) {
        // Use system menu color, then brighten by +20 each channel for dark mode
        COLORREF base = GetSysColor(COLOR_MENU);
        int r = GetRValue(base) + 20;
        int g = GetGValue(base) + 20;
        int b = GetBValue(base) + 20;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return RGB(r, g, b);
    } else {
        // In light mode, use the system menu color directly (no brightening, not black)
        return GetSysColor(COLOR_MENU);
    }
}

static COLORREF big_menu_get_border_color(BOOL darkMode) {
    if (darkMode) {
        return RGB(75, 75, 75); // Updated to match submenu color
    }
    return RGB(196, 196, 196);
}

static COLORREF big_menu_get_separator_color(BOOL darkMode) {
    if (darkMode) {
        return RGB(62, 62, 62);
    }
    return RGB(214, 214, 214);
}

static void big_menu_fill_background(HDC hdc, const RECT* rc, COLORREF color) {
    if (!hdc || !rc) return;
    // Use the color passed in (from big_menu_get_background_color)
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, rc, brush ? brush : GetSysColorBrush(COLOR_MENU));
    if (brush) DeleteObject(brush);
}

static void big_menu_fill_translucent_rect(HDC hdc, const RECT* rc, COLORREF color, BYTE alpha) {
    if (!hdc || !rc) return;
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    if (w <= 0 || h <= 0) return;

    HDC memdc = CreateCompatibleDC(hdc);
    if (!memdc) return;

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(memdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib) { DeleteDC(memdc); return; }
    HBITMAP oldBmp = (HBITMAP)SelectObject(memdc, dib);

    HBRUSH brush = CreateSolidBrush(color);
    RECT r = { 0, 0, w, h };
    FillRect(memdc, &r, brush);
    DeleteObject(brush);

    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = alpha;
    bf.AlphaFormat = 0;
    AlphaBlend(hdc, rc->left, rc->top, w, h, memdc, 0, 0, w, h, bf);

    SelectObject(memdc, oldBmp);
    DeleteObject(dib);
    DeleteDC(memdc);
}

static void big_menu_draw_fake_rounded_highlight(HDC hdc, const RECT* rc, int radius, COLORREF fillColor, COLORREF backgroundColor) {
    RECT fill;
    HBRUSH fillBrush;

    if (!hdc || !rc) return;
    fill = *rc;
    if (radius <= 0) {
        fillBrush = CreateSolidBrush(fillColor);
        FillRect(hdc, &fill, fillBrush ? fillBrush : GetSysColorBrush(COLOR_HIGHLIGHT));
        if (fillBrush) DeleteObject(fillBrush);
        return;
    }

    fillBrush = CreateSolidBrush(fillColor);
    if (!fillBrush) {
        FillRect(hdc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
        return;
    }

    FillRect(hdc, &fill, fillBrush);
    DeleteObject(fillBrush);

    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            enum { SUBSAMPLES = 6 };
            int inside = 0;
            int total = SUBSAMPLES * SUBSAMPLES;
            COLORREF color;

            for (int sy = 0; sy < SUBSAMPLES; ++sy) {
                for (int sx = 0; sx < SUBSAMPLES; ++sx) {
                    double sampleX = (double)x + ((double)sx + 0.5) / (double)SUBSAMPLES;
                    double sampleY = (double)y + ((double)sy + 0.5) / (double)SUBSAMPLES;
                    double dx = (double)radius - sampleX;
                    double dy = (double)radius - sampleY;
                    if ((dx * dx + dy * dy) <= (double)(radius * radius)) {
                        inside++;
                    }
                }
            }

            if (inside == total) continue;
            if (inside <= 0) {
                color = backgroundColor;
            } else {
                int alpha = (inside * 255) / total;
                color = big_menu_blend_colors(backgroundColor, fillColor, alpha);
            }

            SetPixelV(hdc, rc->left + x, rc->top + y, color);
            SetPixelV(hdc, rc->right - 1 - x, rc->top + y, color);
            SetPixelV(hdc, rc->left + x, rc->bottom - 1 - y, color);
            SetPixelV(hdc, rc->right - 1 - x, rc->bottom - 1 - y, color);
        }
    }
}

static void big_menu_draw_highlight_border(HDC hdc, const RECT* rc, int radius, COLORREF borderColor) {
    HPEN pen;
    HPEN oldPen;
    HBRUSH oldBrush;

    if (!hdc || !rc) return;
    pen = CreatePen(PS_SOLID, 1, borderColor);
    if (!pen) return;
    oldPen = (HPEN)SelectObject(hdc, pen);
    oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    if (radius > 0) {
        RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius * 2, radius * 2);
    } else {
        Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void big_menu_draw_aa_rounded_border(HDC hdc, const RECT* rc, int radius, COLORREF borderColor, COLORREF backgroundColor) {
    HBRUSH brush;
    RECT line;
    int left;
    int top;
    int right;
    int bottom;
    int innerRadius;
    int samples;
    int totalSamples;

    if (!hdc || !rc) return;
    if (radius <= 0) {
        big_menu_draw_highlight_border(hdc, rc, radius, borderColor);
        return;
    }

    left = rc->left;
    top = rc->top;
    right = rc->right;
    bottom = rc->bottom;
    if (right - left <= 2 || bottom - top <= 2) return;

    radius = min(radius, min((right - left) / 2, (bottom - top) / 2));
    innerRadius = max(0, radius - 1);
    samples = 4;
    totalSamples = samples * samples;

    brush = CreateSolidBrush(borderColor);
    if (brush) {
        line = (RECT){ left + radius, top, right - radius, top + 1 };
        FillRect(hdc, &line, brush);
        line = (RECT){ left + radius, bottom - 1, right - radius, bottom };
        FillRect(hdc, &line, brush);
        line = (RECT){ left, top + radius, left + 1, bottom - radius };
        FillRect(hdc, &line, brush);
        line = (RECT){ right - 1, top + radius, right, bottom - radius };
        FillRect(hdc, &line, brush);
        DeleteObject(brush);
    }

    for (int y = 0; y < radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            int covered = 0;
            for (int sy = 0; sy < samples; ++sy) {
                for (int sx = 0; sx < samples; ++sx) {
                    double px = (double)x + ((double)sx + 0.5) / (double)samples;
                    double py = (double)y + ((double)sy + 0.5) / (double)samples;
                    double dx = (double)radius - px;
                    double dy = (double)radius - py;
                    double dist2 = dx * dx + dy * dy;
                    if (dist2 <= (double)(radius * radius) &&
                        dist2 >= (double)(innerRadius * innerRadius)) {
                        covered++;
                    }
                }
            }
            if (covered > 0) {
                COLORREF color = big_menu_blend_colors(backgroundColor, borderColor, (covered * 255) / totalSamples);
                SetPixelV(hdc, left + x, top + y, color);
                SetPixelV(hdc, right - 1 - x, top + y, color);
                SetPixelV(hdc, left + x, bottom - 1 - y, color);
                SetPixelV(hdc, right - 1 - x, bottom - 1 - y, color);
            }
        }
    }
}

static COLORREF big_menu_get_system_menu_text_color(void) {
    return RGB(24, 24, 24);
}

static COLORREF big_menu_get_text_color(COLORREF background) {
    int lum = ((30 * GetRValue(background)) + (59 * GetGValue(background)) + (11 * GetBValue(background))) / 100;
    return (lum > 140) ? RGB(24, 24, 24) : RGB(240, 240, 240);
}

static COLORREF big_menu_get_disabled_text_color(COLORREF background, COLORREF text) {
    return big_menu_blend_colors(background, text, 108);
}

static COLORREF big_menu_get_highlight_text_color(COLORREF highlight) {
    int lum = ((30 * GetRValue(highlight)) + (59 * GetGValue(highlight)) + (11 * GetBValue(highlight))) / 100;
    return (lum > 140) ? RGB(32, 32, 32) : RGB(255, 255, 255);
}

static COLORREF big_menu_get_highlight_color(void) {
    // Try to get the Windows accent color; fallback to system highlight if unavailable.
    COLORREF accent = 0;
    if (theme_get_accent(&accent)) {
        wchar_t buf[128];
        wsprintfW(buf, L"[big_menu_get_highlight_color] Retrieved accent color: 0x%06X\n", accent);
        OutputDebugStringW(buf);
        return accent;
    }
    return GetSysColor(COLOR_HIGHLIGHT);
}

static UINT big_menu_resolve_animation_direction(const BIG_MENU_STATE* state) {
    if (!state || !state->params) return WMM_BIG_MENU_ANIM_AUTO;
    if (state->params->animationDirection == WMM_BIG_MENU_ANIM_AUTO) return WMM_BIG_MENU_ANIM_AUTO;
    if (state->params->animationDirection == WMM_BIG_MENU_ANIM_TOP ||
        state->params->animationDirection == WMM_BIG_MENU_ANIM_BOTTOM ||
        state->params->animationDirection == WMM_BIG_MENU_ANIM_LEFT ||
        state->params->animationDirection == WMM_BIG_MENU_ANIM_RIGHT) {
        return state->params->animationDirection;
    }
    return WMM_BIG_MENU_ANIM_BOTTOM;
}

static double big_menu_ease_out_expo(double value) {
    if (value >= 1.0) return 1.0;
    return 1.0 - pow(2.0, -10.0 * value);
}

static int big_menu_round_double(double value) {
    return (int)((value >= 0.0) ? (value + 0.5) : (value - 0.5));
}

static POINT big_menu_get_animation_start(const BIG_MENU_STATE* state, POINT target) {
    POINT start = target;
    UINT direction = big_menu_resolve_animation_direction(state);
    int offset = 0;

    if (!state) return target;
    if (direction == WMM_BIG_MENU_ANIM_AUTO) return target;

    offset = MulDiv(18, state->dpi, 96);

    switch (direction) {
    case WMM_BIG_MENU_ANIM_TOP:
        start.y = target.y - offset;
        break;
    case WMM_BIG_MENU_ANIM_LEFT:
        start.x = target.x - offset;
        break;
    case WMM_BIG_MENU_ANIM_RIGHT:
        start.x = target.x + offset;
        break;
    case WMM_BIG_MENU_ANIM_BOTTOM:
    default:
        start.y = target.y + offset;
        break;
    }

    return start;
}

static HFONT big_menu_create_font(void) {
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };

    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        return CreateFontIndirectW(&metrics.lfMenuFont);
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static void big_menu_apply_window_theme(HWND hwnd, BOOL darkMode) {
    BOOL dark = darkMode ? TRUE : FALSE;

    if (!hwnd) return;
    SetWindowTheme(hwnd, L"Menu", NULL);
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    if (big_menu_is_windows_11_or_greater()) {
        DWM_WINDOW_CORNER_PREFERENCE_LOCAL corner = DWMWCP_ROUND_LOCAL;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    }
}

// Enable a translucent backdrop that matches the native Win32 submenu look on
// the current OS. On Windows 11+ we prefer DWM's system backdrop in the
// transient-window flavor (this is what the menu flyout uses), which keeps
// the tint under DWM's control so colors match native menus. On Windows 10
// we fall back to the undocumented SetWindowCompositionAttribute blur with a
// subtle theme-aware tint, and finally to DwmEnableBlurBehindWindow.
static BOOL big_menu_enable_blur(HWND hwnd, BOOL darkMode) {
    HMODULE user32;
    SETWINDOWCOMPOSITIONATTRIBUTE setWca;
    ACCENT_POLICY policy;
    WINDOWCOMPOSITIONATTRIBDATA data;
    BOOL win11;

    if (!hwnd) return FALSE;

    win11 = big_menu_is_windows_11_or_greater();
    // Only apply acrylic/blur if darkMode is TRUE (dark mode)
    if (!darkMode) {
        // In light mode, skip transparency/blur entirely
        return FALSE;
    }

    user32 = GetModuleHandleW(L"user32.dll");
    setWca = user32 ? (SETWINDOWCOMPOSITIONATTRIBUTE)GetProcAddress(user32, "SetWindowCompositionAttribute") : NULL;

    if (setWca) {
        // Subtle tint that tracks the theme. ABGR in little-endian DWORD form
        // as SetWindowCompositionAttribute expects: 0xAABBGGRR. We pick a low
        // alpha so the system-provided blur dominates -- the paint pass adds
        // the rest of the opacity via big_menu_fill_translucent_rect.
        DWORD tint = 0x802C2C2C; // Only use dark tint for dark mode
        ZeroMemory(&policy, sizeof(policy));
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.GradientColor = tint;

        data.Attrib = WCA_ACCENT_POLICY;
        data.pvData = &policy;
        data.cbData = sizeof(policy);
        BOOL acrylicOk = setWca(hwnd, &data);
        wchar_t buf[128];
        wsprintfW(buf, L"[WinMacMenu] SetWindowCompositionAttribute ACRYLIC result=%d\n", acrylicOk);
        OutputDebugStringW(buf);
        if (acrylicOk) {
            return TRUE;
        }
    }

    // Last-resort fallback: plain DWM blur behind (no tint control).
    {
        DWM_BLURBEHIND blur;
        ZeroMemory(&blur, sizeof(blur));
        blur.dwFlags = DWM_BB_ENABLE;
        blur.fEnable = TRUE;
        HRESULT hr = DwmEnableBlurBehindWindow(hwnd, &blur);
        wchar_t buf[128];
        wsprintfW(buf, L"[WinMacMenu] DwmEnableBlurBehindWindow hr=0x%08X\n", hr);
        OutputDebugStringW(buf);
        return SUCCEEDED(hr);
    }
}

static BIG_MENU_STATE* big_menu_get_state(HWND hwnd) {
    return (BIG_MENU_STATE*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static BOOL big_menu_item_is_selectable(const WMM_BIG_MENU_ITEM* item) {
    if (!item) return FALSE;
    if (item->kind == WMM_BIG_MENU_ITEM_SEPARATOR) return FALSE;
    if ((item->flags & WMM_BIG_MENU_ITEM_FLAG_DISABLED) != 0) return FALSE;
    return TRUE;
}

static int big_menu_get_item_top(const BIG_MENU_STATE* state, int index) {
    int top = 0;
    UINT i;

    for (i = 0; i < (UINT)index && i < state->params->itemCount; ++i) {
        top += (state->params->items[i].kind == WMM_BIG_MENU_ITEM_SEPARATOR) ? state->separatorHeight : state->itemHeight;
    }
    return top;
}

static RECT big_menu_get_item_rect(const BIG_MENU_STATE* state, int index) {
    RECT rc;
    int top;
    int height;

    top = big_menu_get_item_top(state, index);
    height = (state->params->items[index].kind == WMM_BIG_MENU_ITEM_SEPARATOR) ? state->separatorHeight : state->itemHeight;
    rc.left = 0;
    rc.top = top;
    rc.right = state->width;
    rc.bottom = top + height;
    return rc;
}

static int big_menu_hit_test(const BIG_MENU_STATE* state, POINT pt) {
    if (!state || !state->params) return -1;
    // Reject points that are not inside the root menu's client rectangle.
    // Without the X check, every row behaves like an infinite horizontal
    // strip: when the cursor is inside an open native submenu (drawn to the
    // right of the root menu), the WH_MSGFILTER hook would convert its screen
    // point to root-client coords, match the Y to a root item, and call
    // EndMenu() -- hijacking the submenu whenever the cursor's row happened
    // to align with a different root item.
    if (pt.x < 0 || pt.x >= state->width) return -1;
    if (pt.y < 0) return -1;
    int y = pt.y;
    int top = 0;
    for (UINT i = 0; i < (UINT)state->params->itemCount; ++i) {
        int height = (state->params->items[i].kind == WMM_BIG_MENU_ITEM_SEPARATOR) ? state->separatorHeight : state->itemHeight;
        if (y >= top && y < top + height) return (int)i;
        top += height;
    }
    return -1;
}

static void big_menu_set_hot_item(BIG_MENU_STATE* state, int index) {
    if (!state) return;
    if (index < -1) index = -1;
    if (index >= (int)state->params->itemCount) index = -1;
    if (state->hoverIndex == index) return;
    int old = state->hoverIndex;
    state->hoverIndex = index;
    if (old >= 0) {
        RECT rc = big_menu_get_item_rect(state, old);
        rc.top -= 1;
        rc.bottom += 1;
        InvalidateRect(state->hwnd, &rc, FALSE);
    }
    if (index >= 0) {
        RECT rc = big_menu_get_item_rect(state, index);
        rc.top -= 1;
        rc.bottom += 1;
        InvalidateRect(state->hwnd, &rc, FALSE);
    }
}

static int big_menu_find_next_selectable(const BIG_MENU_STATE* state, int start, int dir) {
    if (!state || !state->params || dir == 0) return -1;
    int count = (int)state->params->itemCount;
    int i = start + dir;
    while (i >= 0 && i < count) {
        const WMM_BIG_MENU_ITEM* item = &state->params->items[i];
        if (item->kind != WMM_BIG_MENU_ITEM_SEPARATOR && (item->flags & WMM_BIG_MENU_ITEM_FLAG_DISABLED) == 0) return i;
        i += dir;
    }
    return -1;
}
    
static LRESULT CALLBACK big_menu_msgfilter_proc(int code, WPARAM wParam, LPARAM lParam) {
    MSG* msg = (MSG*)lParam;
    if (code < 0 || !msg || !g_bigMenuHookState) return CallNextHookEx(g_bigMenuMsgHook, code, wParam, lParam);
    if (msg->message == WM_MOUSEMOVE) {
        POINT ptClient;
        int hit;
        POINT ptScreen = msg->pt;
        ptClient = ptScreen;
        ScreenToClient(g_bigMenuHookState->hwnd, &ptClient);
        hit = big_menu_hit_test(g_bigMenuHookState, ptClient);
            g_bigMenuHoverIndexFromHook = hit;
            if (hit >= 0 && hit != g_bigMenuHookState->submenuIndex) {
                const WMM_BIG_MENU_ITEM* item = &g_bigMenuHookState->params->items[hit];
                if (item->kind == WMM_BIG_MENU_ITEM_SUBMENU && big_menu_item_is_selectable(item)) {
                    g_bigMenuPendingHoverIndex = hit;
                    EndMenu();
                    return 1;
                }
                if (item->kind != WMM_BIG_MENU_ITEM_SUBMENU) {
                    g_bigMenuPendingHoverIndex = -1;
                    EndMenu();
                    return 1;
                }
            }
        }
        if (msg->message == WM_LBUTTONDOWN || msg->message == WM_RBUTTONDOWN || msg->message == WM_MBUTTONDOWN ||
            msg->message == WM_NCLBUTTONDOWN || msg->message == WM_NCRBUTTONDOWN || msg->message == WM_NCMBUTTONDOWN) {
            POINT ptScreen = msg->pt;
            HWND hwndAtPoint = WindowFromPoint(ptScreen);
            if (hwndAtPoint && hwndAtPoint != g_bigMenuHookState->hwnd) {
                WCHAR className[32] = L"";
                GetClassNameW(hwndAtPoint, className, ARRAYSIZE(className));
                if (lstrcmpW(className, L"#32768") != 0) {
                    g_bigMenuHookState->closeAfterSubmenu = TRUE;
                    EndMenu();
                    return 1;
                }
            }
        }

    return CallNextHookEx(g_bigMenuMsgHook, code, wParam, lParam);
}

static LRESULT CALLBACK big_menu_cbt_proc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_CREATEWND) {
        HWND hwnd = (HWND)wParam;
        WCHAR className[32] = L"";
        if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) && lstrcmpW(className, L"#32768") == 0) {
            // Apply only the theme hint (dark mode + rounded corners on Win11).
            // We deliberately do NOT call big_menu_enable_blur on native menu
            // windows: forcing DWMWA_SYSTEMBACKDROP_TYPE or an accent policy
            // composition onto a #32768 popup interferes with its internal
            // paint cycle and the menu loses its "latched" highlight on the
            // item whose sub-submenu is open (and the last-hovered highlight
            // when the cursor drifts off). On Windows 11 native menu flyouts
            // already carry Mica/acrylic automatically, so doing nothing here
            // yields the correct native look AND correct hover behavior.
            big_menu_apply_window_theme(hwnd, g_bigMenuCbtDarkMode);
        }
    }
    return CallNextHookEx(g_bigMenuCbtHook, code, wParam, lParam);
}

static void big_menu_close(BIG_MENU_STATE* state, UINT commandId) {
    if (!state) return;
    wchar_t buf[256];
    wsprintfW(buf, L"[big_menu_close] state=%p, result=%p, commandId=%u\n", state, state ? state->result : NULL, commandId);
    OutputDebugStringW(buf);
    if (state->result) {
        wsprintfW(buf, L"[big_menu_close] Writing result: cbSize=%u, selectedCommandId=%u\n", (unsigned)sizeof(*state->result), commandId);
        OutputDebugStringW(buf);
        state->result->cbSize = sizeof(*state->result);
        // Only set selectedCommandId if not already set to a nonzero value
        if (state->result->selectedCommandId == 0) {
            state->result->selectedCommandId = commandId;
        } else {
            wsprintfW(buf, L"[big_menu_close] Not overwriting existing selectedCommandId=%u\n", state->result->selectedCommandId);
            OutputDebugStringW(buf);
        }
    } else {
        OutputDebugStringW(L"[big_menu_close] state->result is NULL!\n");
    }
    state->running = FALSE;
    if (state->hwnd && IsWindow(state->hwnd)) DestroyWindow(state->hwnd);
}

static UINT big_menu_open_native_submenu(BIG_MENU_STATE* state, int index) {
    RECT itemRc;
    POINT screenPt;
    UINT cmd;
    int openIndex;

    if (!state || index < 0 || index >= (int)state->params->itemCount) return 0;
    if (state->params->items[index].kind != WMM_BIG_MENU_ITEM_SUBMENU || !state->params->items[index].nativeSubmenu) return 0;
    openIndex = index;

    for (;;) {
        itemRc = big_menu_get_item_rect(state, openIndex);
        screenPt.x = state->width - 2;
        screenPt.y = itemRc.top;
        ClientToScreen(state->hwnd, &screenPt);

        state->submenuIndex = openIndex;
        state->inSubmenu = TRUE;
        state->closeAfterSubmenu = FALSE;
        g_bigMenuHookState = state;
        g_bigMenuPendingHoverIndex = -1;
        g_bigMenuHoverIndexFromHook = -1;
        g_bigMenuMsgHook = SetWindowsHookExW(WH_MSGFILTER, big_menu_msgfilter_proc, NULL, GetCurrentThreadId());
        g_bigMenuCbtDarkMode = state->params->darkMode;
        g_bigMenuCbtHook = SetWindowsHookExW(WH_CBT, big_menu_cbt_proc, NULL, GetCurrentThreadId());

        ReleaseCapture();
        if (state->params->showNativeSubmenu) {
            cmd = state->params->showNativeSubmenu(
                state->params->owner ? state->params->owner : state->hwnd,
                state->params->items[openIndex].nativeSubmenu,
                screenPt,
                (UINT)openIndex);
        } else {
            if (state->params->initNativeSubmenu) {
                state->params->initNativeSubmenu(state->params->owner ? state->params->owner : state->hwnd, state->params->items[openIndex].nativeSubmenu, (UINT)openIndex);
            }
            SetForegroundWindow(state->hwnd);
            cmd = TrackPopupMenuEx(
                state->params->items[openIndex].nativeSubmenu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL | TPM_RECURSE,
                screenPt.x,
                screenPt.y,
                state->hwnd,
                NULL);
        }
        if (g_bigMenuMsgHook) {
            UnhookWindowsHookEx(g_bigMenuMsgHook);
            g_bigMenuMsgHook = NULL;
        }
        if (g_bigMenuCbtHook) {
            UnhookWindowsHookEx(g_bigMenuCbtHook);
            g_bigMenuCbtHook = NULL;
        }
        g_bigMenuHookState = NULL;
        SetCapture(state->hwnd);
        state->inSubmenu = FALSE;

        if (state->closeAfterSubmenu) {
            big_menu_close(state, 0);
            return 0;
        }

        if (g_bigMenuHoverIndexFromHook >= 0) {
            big_menu_set_hot_item(state, g_bigMenuHoverIndexFromHook);
        }

        if (cmd != 0) {
            big_menu_close(state, cmd);
            return cmd;
        }

        if (g_bigMenuPendingHoverIndex >= 0 && g_bigMenuPendingHoverIndex != openIndex) {
            openIndex = g_bigMenuPendingHoverIndex;
            continue;
        }

        if (cmd == 0) {
            state->lastSubmenuCloseIndex = openIndex;
            state->lastSubmenuCloseTick = GetTickCount();
        }
        state->submenuIndex = -1;
        return 0;
    }
}

static void big_menu_measure(BIG_MENU_STATE* state) {
    HDC hdc;
    HFONT oldFont;
    TEXTMETRICW tm;
    UINT i;
    int maxTextWidth = 0;

    state->dpi = GetDpiForWindow(state->params->owner ? state->params->owner : GetDesktopWindow());
    if (state->dpi <= 0) state->dpi = 96;
    state->iconSize = MulDiv(state->params->largeRootIcons ? 32 : 16, state->dpi, 96);
    state->leftPad = MulDiv(8, state->dpi, 96);
    state->iconGap = MulDiv(10, state->dpi, 96);
    state->rightPad = MulDiv(12, state->dpi, 96);
    state->submenuPad = MulDiv(18, state->dpi, 96);
    state->selectionRadius = big_menu_is_windows_11_or_greater() ? MulDiv(4, state->dpi, 96) : 0;

    hdc = GetDC(NULL);
    oldFont = (HFONT)SelectObject(hdc, state->font);
    GetTextMetricsW(hdc, &tm);
    state->itemHeight = max(MulDiv(40, state->dpi, 96), max(state->iconSize + MulDiv(8, state->dpi, 96), tm.tmHeight + MulDiv(12, state->dpi, 96)));
    state->separatorHeight = MulDiv(10, state->dpi, 96);

    for (i = 0; i < state->params->itemCount; ++i) {
        const WMM_BIG_MENU_ITEM* item = &state->params->items[i];
        RECT rc = { 0, 0, 1, 1 };
        if (item->kind == WMM_BIG_MENU_ITEM_SEPARATOR) continue;
        DrawTextW(hdc, item->text, -1, &rc, DT_SINGLELINE | DT_CALCRECT);
        if ((rc.right - rc.left) > maxTextWidth) maxTextWidth = rc.right - rc.left;
    }
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);

    state->width = state->leftPad + state->iconSize + state->iconGap + maxTextWidth + state->rightPad + state->submenuPad;
    state->width = max(state->width, MulDiv(220, state->dpi, 96));
    state->totalHeight = 0;
    for (i = 0; i < state->params->itemCount; ++i) {
        state->totalHeight += (state->params->items[i].kind == WMM_BIG_MENU_ITEM_SEPARATOR) ? state->separatorHeight : state->itemHeight;
    }
}

static void big_menu_draw_arrow(HDC hdc, RECT rc, COLORREF color, int dpi, BOOL selected) {
    // Draw a bold chevron/less-than arrow, 1px smaller
    (void)selected;
    int stroke = max(2, MulDiv(2, dpi, 96));
    int arrowWidth = MulDiv(5, dpi, 96) - 1;
    int arrowHeight = MulDiv(9, dpi, 96) - 1;
    int x0 = rc.right - MulDiv(10, dpi, 96) - arrowWidth;
    int y0 = rc.top + ((rc.bottom - rc.top) - arrowHeight) / 2;
    POINT pts[3];
    pts[0].x = x0;
    pts[0].y = y0;
    pts[1].x = x0 + arrowWidth;
    pts[1].y = y0 + (arrowHeight / 2);
    pts[2].x = x0;
    pts[2].y = y0 + arrowHeight;
    HPEN pen = CreatePen(PS_SOLID, stroke, color);
    if (!pen) return;
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, pts[0].x, pts[0].y, NULL);
    LineTo(hdc, pts[1].x, pts[1].y);
    LineTo(hdc, pts[2].x, pts[2].y);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void big_menu_paint(BIG_MENU_STATE* state, HDC hdc) {
    HBRUSH sepBrush;
    HBRUSH borderBrush;
    COLORREF highlight;
    if (!theme_get_menu_highlight(&highlight)) {
        highlight = GetSysColor(COLOR_HIGHLIGHT); // fallback to system highlight
    }
    COLORREF backgroundColor = big_menu_get_background_color(state->params->darkMode);
    COLORREF borderColor = big_menu_get_border_color(state->params->darkMode);
    COLORREF separatorColor = big_menu_get_separator_color(state->params->darkMode);
    // Use system highlight and highlight text color to match submenu exactly
    COLORREF highlightTextColor = GetSysColor(COLOR_HIGHLIGHTTEXT);
    // Text color is theme-aware (derived from backgroundColor, which already
    // accounts for darkMode). Do NOT force a single hardcoded color when
    // useBackdrop is on -- that breaks dark mode contrast.
    COLORREF textColor = big_menu_get_text_color(backgroundColor);
    COLORREF disabledTextColor = big_menu_get_disabled_text_color(backgroundColor, textColor);
    UINT i;
    HFONT oldFont;
    RECT backgroundRect;


    backgroundRect.left = 0;
    backgroundRect.top = 0;
    backgroundRect.right = state->width;
    backgroundRect.bottom = state->totalHeight;
    if (state->useBackdrop) {
        // Repaint a translucent base each frame so old hover edges do not persist.
        big_menu_fill_translucent_rect(hdc, &backgroundRect, backgroundColor, 224);
    } else {
        big_menu_fill_background(hdc, &backgroundRect, backgroundColor);
    }

    oldFont = (HFONT)SelectObject(hdc, state->font);
    if (state->useBackdrop) {
        SetBkMode(hdc, TRANSPARENT);
    } else {
        SetBkMode(hdc, OPAQUE);
    }

    for (i = 0; i < state->params->itemCount; ++i) {
        const WMM_BIG_MENU_ITEM* item = &state->params->items[i];
        RECT rc = big_menu_get_item_rect(state, (int)i);
        BOOL disabled = (item->flags & WMM_BIG_MENU_ITEM_FLAG_DISABLED) != 0;
        BOOL selected = ((int)i == state->hoverIndex && big_menu_item_is_selectable(item));
        COLORREF itemTextColor = textColor;

        if (item->kind == WMM_BIG_MENU_ITEM_SEPARATOR) {
            RECT sep = rc;
            sep.top += (state->separatorHeight / 2);
            sep.bottom = sep.top + 1;
            sep.left += state->leftPad;
            sep.right -= state->rightPad;
            sepBrush = CreateSolidBrush(separatorColor);
            FillRect(hdc, &sep, sepBrush ? sepBrush : GetSysColorBrush(COLOR_3DSHADOW));
            if (sepBrush) DeleteObject(sepBrush);
            continue;
        }

        if (selected) {
            RECT sel = rc;
            RECT borderSel;
            BOOL drawBackground = (state->params->highlightFrame != WMM_BIG_MENU_HIGHLIGHT_BORDER);
            BOOL drawBorder = (state->params->highlightFrame != WMM_BIG_MENU_HIGHLIGHT_BACKGROUND);
            int highlightInset = max(3, MulDiv(3, state->dpi, 96));
            if (state->selectionRadius > 0) {
                InflateRect(&sel, -highlightInset, -highlightInset);
            } else {
                InflateRect(&sel, -1, 0);
                sel.top -= 1;
                sel.bottom += 1;
            }
            borderSel = sel;
            if (!drawBackground) {
                borderSel = rc;
                InflateRect(&borderSel, -highlightInset, -highlightInset);
            } else {
                if (borderSel.top <= backgroundRect.top) borderSel.top = backgroundRect.top + 1;
                if (borderSel.bottom >= backgroundRect.bottom) borderSel.bottom = backgroundRect.bottom - 1;
            }
            // Use system highlight text color as default
            COLORREF selectedTextColor = GetSysColor(COLOR_HIGHLIGHTTEXT);
            // If not keeping normal text color, and dark mode is active, prefer a dark text color
            // so highlighted items in dark mode show dark text over the accent highlight (user wants revert)
            if (!state->params->keepLargeMenuHighlightTextColor && state->params->darkMode) {
                selectedTextColor = RGB(32,32,32);
            }
            // Debug: log selection color decision
            {
                WCHAR dbg[256];
                wsprintfW(dbg, L"[big_menu] keep=%d dark=%d highlight=0x%06X selectedText=0x%06X\n", (int)state->params->keepLargeMenuHighlightTextColor, (int)state->params->darkMode, (unsigned)highlight, (unsigned)selectedTextColor);
                OutputDebugStringW(dbg);
            }
            if (drawBackground) {
                big_menu_draw_fake_rounded_highlight(hdc, &sel, state->selectionRadius, highlight, backgroundColor);
                SetBkColor(hdc, highlight);
            }
            if (drawBorder) {
                COLORREF stroke = drawBackground ? big_menu_blend_colors(highlight, selectedTextColor, 96) : highlight;
                if (state->selectionRadius > 0)
                    big_menu_draw_aa_rounded_border(hdc, &borderSel, state->selectionRadius, stroke, backgroundColor);
                else
                    big_menu_draw_highlight_border(hdc, &borderSel, state->selectionRadius, stroke);
            }
            if (state->params->keepLargeMenuHighlightTextColor || !drawBackground) itemTextColor = textColor;
            else itemTextColor = selectedTextColor;
            SetTextColor(hdc, itemTextColor);
        } else {
            if (disabled) itemTextColor = disabledTextColor;
            SetBkColor(hdc, backgroundColor);
            SetTextColor(hdc, itemTextColor);
        }

        if (item->icon) {
            int x = rc.left + state->leftPad;
            int y = rc.top + ((rc.bottom - rc.top) - state->iconSize) / 2;
            DrawIconEx(hdc, x, y, item->icon, state->iconSize, state->iconSize, 0, NULL, DI_NORMAL);
        }

        {
            RECT textRc = rc;
            textRc.left += state->leftPad + state->iconSize + state->iconGap;
            textRc.right -= state->rightPad + ((item->kind == WMM_BIG_MENU_ITEM_SUBMENU) ? state->submenuPad : 0);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, itemTextColor);
            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm);
            int textY = textRc.top + ((textRc.bottom - textRc.top - tm.tmHeight) / 2);
            ExtTextOutW(hdc, textRc.left, textY,
                        ETO_CLIPPED,
                        &textRc, item->text, (int)wcslen(item->text), NULL);
        }

        if (item->kind == WMM_BIG_MENU_ITEM_SUBMENU) {
            big_menu_draw_arrow(hdc, rc, itemTextColor, state->dpi, selected);
        }
    }

    if (!state->useBackdrop) {
        borderBrush = CreateSolidBrush(borderColor);
        FrameRect(hdc, &backgroundRect, borderBrush ? borderBrush : GetSysColorBrush(COLOR_WINDOWFRAME));
        if (borderBrush) DeleteObject(borderBrush);
    }

    SelectObject(hdc, oldFont);
}

static LRESULT CALLBACK big_menu_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_SETCURSOR) {
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            return TRUE;
        }
    BIG_MENU_STATE* state = big_menu_get_state(hwnd);

    switch (msg) {
    case WM_NCCREATE:
        {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
    case WM_CREATE:
        state = big_menu_get_state(hwnd);
        if (!state) return -1;
        state->hwnd = hwnd;
        big_menu_apply_window_theme(hwnd, state->params->darkMode);
        return 0;
    case WM_MOUSEMOVE:
        if (state) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int hit;
            if (!state->mouseTracked) {
                TrackMouseEvent(&tme);
                state->mouseTracked = TRUE;
            }
            hit = big_menu_hit_test(state, pt);
            big_menu_set_hot_item(state, hit);
            if (!state->inSubmenu && hit >= 0 && hit < (int)state->params->itemCount) {
                const WMM_BIG_MENU_ITEM* item = &state->params->items[hit];
                if (item->kind == WMM_BIG_MENU_ITEM_SUBMENU && big_menu_item_is_selectable(item) && state->submenuIndex != hit) {
                    if (state->lastSubmenuCloseIndex == hit) {
                        DWORD elapsed = GetTickCount() - state->lastSubmenuCloseTick;
                        if (elapsed < 200) return 0;
                    }
                    big_menu_open_native_submenu(state, hit);
                } else if (item->kind != WMM_BIG_MENU_ITEM_SUBMENU) {
                    state->submenuIndex = -1;
                }
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) {
            state->mouseTracked = FALSE;
            if (!state->inSubmenu) {
                state->submenuIndex = -1;
                big_menu_set_hot_item(state, -1);
            }
        }
        return 0;
    case WM_TIMER:
        if (state && wParam == BIG_MENU_ANIM_TIMER_ID && state->animating) {
            double elapsedMs = 0.0;
            double progress = 0.0;
            double moveProgress = 0.0;
            int x;
            int y;

            if (state->animationDurationMs == 0) state->animationDurationMs = BIG_MENU_ANIM_DURATION_MS;
            if (state->useQpc) {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                elapsedMs = (double)(now.QuadPart - state->animationStartQpc.QuadPart) * 1000.0 / (double)state->animationQpcFreq.QuadPart;
            } else {
                DWORD nowTick = GetTickCount();
                elapsedMs = (double)(nowTick - state->animationStartTick);
            }
            progress = elapsedMs / (double)state->animationDurationMs;
            if (progress > 1.0) progress = 1.0;
            moveProgress = big_menu_ease_out_expo(progress);

            x = state->animationStart.x + big_menu_round_double((state->animationTarget.x - state->animationStart.x) * moveProgress);
            y = state->animationStart.y + big_menu_round_double((state->animationTarget.y - state->animationStart.y) * moveProgress);

            SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);

            if (progress >= 1.0) {
                KillTimer(hwnd, BIG_MENU_ANIM_TIMER_ID);
                if (state->highResTimerActive) {
                    timeEndPeriod(1);
                    state->highResTimerActive = FALSE;
                }
                state->animating = FALSE;
                SetWindowPos(hwnd, NULL, state->animationTarget.x, state->animationTarget.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
            }
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_POINTERDOWN:
        if (state) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int hit = big_menu_hit_test(state, pt);
            wchar_t buf[128];
            wsprintfW(buf, L"[BigMenu Debug] MouseDown at (%d,%d), hit index: %d\n", pt.x, pt.y, hit);
            OutputDebugStringW(buf);
            if (hit >= 0) big_menu_set_hot_item(state, hit);
            else big_menu_close(state, 0);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state && !state->inSubmenu) {
            big_menu_close(state, 0);
        }
        return 0;
    case WM_CANCELMODE:
        if (state) {
            if (state->inSubmenu) {
                state->closeAfterSubmenu = TRUE;
                EndMenu();
            } else {
                big_menu_close(state, 0);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int hit = big_menu_hit_test(state, pt);
            wchar_t buf[256];
            wsprintfW(buf, L"[BigMenu Debug] MouseUp at (%d,%d), hit index: %d\n", pt.x, pt.y, hit);
            OutputDebugStringW(buf);
            if (hit < 0) {
                OutputDebugStringW(L"[BigMenu Debug] MouseUp: No item hit, closing with commandId 0\n");
                big_menu_close(state, 0);
            } else {
                const WMM_BIG_MENU_ITEM* dbg_item = &state->params->items[hit];
                wsprintfW(buf, L"[BigMenu Debug] hit=%d kind=%d flags=0x%X selectable=%d commandId=%u\n", hit, dbg_item->kind, dbg_item->flags, big_menu_item_is_selectable(dbg_item), dbg_item->commandId);
                OutputDebugStringW(buf);
                if (big_menu_item_is_selectable(dbg_item)) {
                    if (dbg_item->kind == WMM_BIG_MENU_ITEM_SUBMENU) {
                        OutputDebugStringW(L"[BigMenu Debug] MouseUp: Submenu item, opening native submenu\n");
                        big_menu_open_native_submenu(state, hit);
                    } else {
                        wsprintfW(buf, L"[BigMenu Debug] MouseUp: Closing menu with commandId: %u\n", dbg_item->commandId);
                        OutputDebugStringW(buf);
                        big_menu_close(state, dbg_item->commandId);
                    }
                } else {
                    OutputDebugStringW(L"[BigMenu Debug] MouseUp: Item not selectable, not closing\n");
                }
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (!state) return 0;
        switch (wParam) {
        case VK_ESCAPE:
            big_menu_close(state, 0);
            return 0;
        case VK_DOWN:
            if (state->keyboardIndex < 0) state->keyboardIndex = big_menu_find_next_selectable(state, -1, +1);
            else state->keyboardIndex = big_menu_find_next_selectable(state, state->keyboardIndex, +1);
            state->hoverIndex = state->keyboardIndex;
            {
                RECT rc = big_menu_get_item_rect(state, state->keyboardIndex);
                rc.top -= 1;
                rc.bottom += 1;
                InvalidateRect(hwnd, &rc, FALSE);
            }
            return 0;
        case VK_UP:
            if (state->keyboardIndex < 0) state->keyboardIndex = big_menu_find_next_selectable(state, (int)state->params->itemCount, -1);
            else state->keyboardIndex = big_menu_find_next_selectable(state, state->keyboardIndex, -1);
            state->hoverIndex = state->keyboardIndex;
            {
                RECT rc = big_menu_get_item_rect(state, state->keyboardIndex);
                rc.top -= 1;
                rc.bottom += 1;
                InvalidateRect(hwnd, &rc, FALSE);
            }
            return 0;
        case VK_RETURN:
        case VK_RIGHT:
            if (state->keyboardIndex >= 0 && state->keyboardIndex < (int)state->params->itemCount) {
                const WMM_BIG_MENU_ITEM* item = &state->params->items[state->keyboardIndex];
                wchar_t buf[256];
                wsprintfW(buf, L"[BigMenu Debug] VK_RETURN/RIGHT: keyboardIndex=%d kind=%d selectable=%d commandId=%u\n", state->keyboardIndex, item->kind, big_menu_item_is_selectable(item), item->commandId);
                OutputDebugStringW(buf);
                if (item->kind == WMM_BIG_MENU_ITEM_SUBMENU) {
                    OutputDebugStringW(L"[BigMenu Debug] VK_RETURN/RIGHT: Submenu item, opening native submenu\n");
                    big_menu_open_native_submenu(state, state->keyboardIndex);
                } else if (big_menu_item_is_selectable(item)) {
                    OutputDebugStringW(L"[BigMenu Debug] VK_RETURN/RIGHT: Closing menu with commandId\n");
                    big_menu_close(state, item->commandId);
                } else {
                    OutputDebugStringW(L"[BigMenu Debug] VK_RETURN/RIGHT: Item not selectable, not closing\n");
                }
            }
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (state && !state->inSubmenu) {
            big_menu_close(state, 0);
        }
        return 0;
    case WM_PAINT:
        if (state) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HDC memdc = CreateCompatibleDC(hdc);
            HBITMAP membmp = CreateCompatibleBitmap(hdc, state->width, state->totalHeight);
            HBITMAP oldbmp = membmp ? (HBITMAP)SelectObject(memdc, membmp) : NULL;
            if (memdc && membmp) {
                big_menu_paint(state, memdc);
                BitBlt(hdc, 0, 0, state->width, state->totalHeight, memdc, 0, 0, SRCCOPY);
            } else {
                big_menu_paint(state, hdc);
            }
            if (oldbmp) SelectObject(memdc, oldbmp);
            if (membmp) DeleteObject(membmp);
            if (memdc) DeleteDC(memdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        if (state) {
            state->running = FALSE;
            if (state->animating) {
                KillTimer(hwnd, BIG_MENU_ANIM_TIMER_ID);
                state->animating = FALSE;
            }
            if (state->highResTimerActive) {
                timeEndPeriod(1);
                state->highResTimerActive = FALSE;
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static ATOM big_menu_register_window_class(void) {
    static ATOM atom = 0;
    WNDCLASSW wc;

    if (atom) return atom;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = big_menu_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = kBigMenuClassName;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    atom = RegisterClassW(&wc);
    return atom;
}

__declspec(dllexport) UINT WINAPI WinMacMenu_GetBigMenuApiVersion(void) {
    return WMM_BIG_MENU_API_VERSION;
}

__declspec(dllexport) BOOL WINAPI WinMacMenu_ShowBigMenu(const WMM_BIG_MENU_SHOW_PARAMS* params, WMM_BIG_MENU_RESULT* result) {
    BIG_MENU_STATE state;
    HWND hwnd;
    MSG msg;
    RECT wa;
    HMONITOR monitor;
    MONITORINFO mi;
    int x;
    int y;
    static BOOL s_debugShown = FALSE;

    if (!s_debugShown) {
        s_debugShown = TRUE;
    }

    if (!params || params->cbSize != sizeof(*params) || params->apiVersion != WMM_BIG_MENU_API_VERSION) return FALSE;
    if (!result || result->cbSize != sizeof(*result)) return FALSE;
    if (!params->items || params->itemCount == 0) return FALSE;
    if (!big_menu_register_window_class()) return FALSE;

    ZeroMemory(&state, sizeof(state));
    state.params = params;
    state.result = result;
    state.font = big_menu_create_font();
    if (!state.font) return FALSE;
    state.hoverIndex = -1;
    state.keyboardIndex = -1;
    state.submenuIndex = -1;
    state.lastSubmenuCloseIndex = -1;
    state.lastSubmenuCloseTick = 0;
    state.running = TRUE;
    // Disable transparency/acrylic globally and always use opaque painting.
    state.useBackdrop = FALSE;
    big_menu_measure(&state);

    monitor = MonitorFromPoint(params->screenPt, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    wa = mi.rcWork;

    x = params->screenPt.x;
    y = params->screenPt.y;
    if (!params->pointerRelative) {
        if (params->hPlacement == 1) x -= (state.width / 2);
        else if (params->hPlacement == 2) x -= state.width;

        if (params->vPlacement == 1) y -= (state.totalHeight / 2);
        else if (params->vPlacement == 2) y -= state.totalHeight;
    }
    if (x + state.width > wa.right) x = wa.right - state.width;
    if (y + state.totalHeight > wa.bottom) y = wa.bottom - state.totalHeight;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;

    {
        POINT target = { x, y };
        state.animationDirection = params->animationDirection;
        state.animationDurationMs = BIG_MENU_ANIM_DURATION_MS;
        state.animationTarget = target;
        state.animationStart = big_menu_get_animation_start(&state, target);
        state.animating = (state.animationDirection != WMM_BIG_MENU_ANIM_AUTO) &&
            (state.animationStart.x != target.x || state.animationStart.y != target.y);
    }

    hwnd = CreateWindowExW(
        (state.animating ? 0 : WS_EX_TOPMOST), // Remove WS_EX_TOOLWINDOW and WS_EX_LAYERED
        kBigMenuClassName,
        L"",
        WS_POPUP,
        state.animating ? state.animationStart.x : x,
        state.animating ? state.animationStart.y : y,
        state.width,
        state.totalHeight,
        params->owner,
        NULL,
        GetModuleHandleW(NULL),
        &state);

    // Force full opacity for layered window (required for DWM blur on some systems)
    // Only do this if not using DWM system backdrop, otherwise let DWM control transparency
    if (hwnd && !state.useBackdrop) {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }
    if (!hwnd) {
        if (state.font != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(state.font);
        return FALSE;
    }

    // Do not force layered alpha; let DWM/system backdrop control transparency so
    // the window matches native submenu composition.
    if (state.useBackdrop) {
        // Enable a translucent backdrop that matches native submenus on this OS.
        BOOL blurResult = big_menu_enable_blur(hwnd, state.params->darkMode);
        wchar_t buf[128];
        wsprintfW(buf, L"[WinMacMenu] big_menu_enable_blur returned: %d\n", blurResult);
        OutputDebugStringW(buf);
    }
    if (state.animating) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    // Removed unconditional SetCapture(hwnd) to avoid loading cursor issue

    if (state.animating) {
        state.animationStartTick = GetTickCount();
        state.useQpc = QueryPerformanceFrequency(&state.animationQpcFreq) != 0;
        if (state.useQpc) QueryPerformanceCounter(&state.animationStartQpc);
        if (timeBeginPeriod(1) == TIMERR_NOERROR) {
            state.highResTimerActive = TRUE;
        }
        SetTimer(hwnd, BIG_MENU_ANIM_TIMER_ID, 5, NULL);
    }

    while (state.running && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsWindow(hwnd)) break;
        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN) {
            if (!state.inSubmenu && msg.hwnd != hwnd) {
                big_menu_close(&state, 0);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (GetCapture() == hwnd) ReleaseCapture();
    if (state.font && state.font != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(state.font);
    return TRUE;
}

static void launch_big_menu_wpf() {
    // Path to the WPF application executable
    LPCWSTR wpfAppPath = L"C:\\Path\\To\\BigMenuWPF.exe";

    // Launch the WPF application
    ShellExecuteW(NULL, L"open", wpfAppPath, NULL, NULL, SW_SHOWNORMAL);
}

// Replace the existing big menu logic with a call to the WPF application
void big_menu_show(const WMM_BIG_MENU_SHOW_PARAMS* params, WMM_BIG_MENU_RESULT* result) {
    // Call the real menu logic so the selected command is returned
    WinMacMenu_ShowBigMenu(params, result);
}
