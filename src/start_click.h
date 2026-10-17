#pragma once
#include <windows.h>
#include <limits.h>
#include <wctype.h>

/* Strict decimal parsing: never turn a truncated/overflowing INI value into a zone. */
static BOOL start_click_integer(const WCHAR* text, int* value) {
    BOOL negative = FALSE;
    unsigned long long n = 0, limit;
    while (iswspace(*text)) ++text;
    if (*text == L'-' || *text == L'+') negative = *text++ == L'-';
    if (*text < L'0' || *text > L'9') return FALSE;
    limit = negative ? (unsigned long long)INT_MAX + 1 : INT_MAX;
    while (*text >= L'0' && *text <= L'9') {
        unsigned digit = *text++ - L'0';
        if (n > (limit - digit) / 10) return FALSE;
        n = n * 10 + digit;
    }
    while (iswspace(*text)) ++text;
    if (*text) return FALSE;
    *value = negative ? (int)(-(long long)n) : (int)n;
    return TRUE;
}

static BOOL start_click_load(LPCWSTR path, int* width, int* height, int* x, int* y) {
    const WCHAR* keys[] = {L"StartClickWidth", L"StartClickHeight", L"StartClickOffsetX", L"StartClickOffsetY"};
    int values[4] = {0};
    BOOL valid = TRUE;
    for (int i = 0; i < 4; ++i) {
        WCHAR text[64];
        DWORD length = GetPrivateProfileStringW(L"Controls", keys[i], i < 2 ? L"" : L"0", text, 64, path);
        if (length >= 63 || !start_click_integer(text, &values[i])) valid = FALSE;
    }
    valid = valid && values[0] > 0 && values[1] > 0;
    *width = valid ? values[0] : 0; *height = valid ? values[1] : 0;
    *x = valid ? values[2] : 0; *y = valid ? values[3] : 0;
    return valid;
}

static BOOL start_click_rect(const RECT* detected, const RECT* taskbar,
                            int width, int height, int x, int y, RECT* result) {
    RECT zone = *detected;
    if (width > 0 && height > 0) {
        long long left = (long long)zone.left + x, top = (long long)zone.top + y;
        long long right = left + width, bottom = top + height;
        /* Invalid screen-coordinate arithmetic disables the override. */
        if (left >= LONG_MIN && top >= LONG_MIN && right <= LONG_MAX && bottom <= LONG_MAX) {
            zone.left = (LONG)left; zone.top = (LONG)top;
            zone.right = (LONG)right; zone.bottom = (LONG)bottom;
        }
    }
    return IntersectRect(result, &zone, taskbar);
}
