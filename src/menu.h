#pragma once
#include <windows.h>

#define IDM_FOLDER_BASE   5000

void ShowWinXMenu(HWND owner, POINT screenPt);
void MenuExecuteCommand(HWND owner, UINT cmd);
void MenuOnMenuSelect(HWND owner, WPARAM wParam, LPARAM lParam);
void MenuOnInitMenuPopup(HWND owner, HMENU hMenu, UINT item, BOOL isSystemMenu);
BOOL MenuOnMenuRButtonUp(HWND owner, UINT itemPos, HMENU hMenu);
BOOL MenuOnMeasureItem(HWND owner, MEASUREITEMSTRUCT* mis);
BOOL MenuOnDrawItem(HWND owner, const DRAWITEMSTRUCT* dis);
BOOL MenuOpenRecentParentFolder(UINT cmd);
void MenuRefreshVisibleMenuWindows(void);

extern BOOL g_shouldReopenMenu;

