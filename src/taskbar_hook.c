#include "taskbar_hook.h"
#include "controls.h"
#include "config.h"
#include "util.h"
#include "start_click.h"
#include <shlwapi.h>
#include <oleacc.h>
#include <UIAutomationClient.h>
#include <stdio.h>

// External reference to global config
extern Config g_cfg;

// Taskbar and start button handles
static HWND g_hTaskbar = NULL;
static HWND g_hStartButton = NULL;
static HHOOK g_hMsgHook = NULL;
static HWND g_hOwnerWnd = NULL;
static BOOL g_suppressNextLeftUp = FALSE;
static BOOL g_suppressNextMiddleUp = FALSE;
static BOOL g_suppressNextRightUp = FALSE;
static SRWLOCK g_startCacheLock = SRWLOCK_INIT;
static RECT g_cachedStart, g_cachedTaskbar;
static HWND g_cachedTaskbarWindow;
static ULONGLONG g_cachedAt;
static LONG g_detectorStarted, g_detectorLegacy;
static BOOL g_cachedLegacy;

static BOOL RefreshTaskbarHandles(void);

static BOOL IsReasonableStartRect(const RECT* rect, const RECT* taskbar) {
    RECT intersection;
    long long width = (long long)rect->right - rect->left;
    long long height = (long long)rect->bottom - rect->top;
    long long thickness = min((long long)taskbar->right - taskbar->left,
                              (long long)taskbar->bottom - taskbar->top);
    return width >= 8 && height >= 8 && thickness > 0 &&
           width <= max(80, thickness * 2) && height <= max(80, thickness * 2) &&
           !EqualRect(rect, taskbar) && IntersectRect(&intersection, rect, taskbar);
}

static BOOL StartName(const WCHAR* text) {
    return text && !lstrcmpiW(text, L"Start");
}

/* Search under the taskbar, not under the click: offsets may extend beyond Start. */
static BOOL StartRectUIA(const RECT* taskbar, RECT* rect) {
    IUIAutomation* automation = NULL;
    IUIAutomationElement* root = NULL;
    IUIAutomationCondition* condition = NULL;
    IUIAutomationElementArray* buttons = NULL;
    BOOL found = FALSE;
    VARIANT type;
    VariantInit(&type);
    type.vt = VT_I4; type.lVal = UIA_ButtonControlTypeId;
    if (FAILED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IUIAutomation, (void**)&automation))) return FALSE;
    if (SUCCEEDED(IUIAutomation_ElementFromHandle(automation, g_hTaskbar, &root)) && root &&
        SUCCEEDED(IUIAutomation_CreatePropertyCondition(automation, UIA_ControlTypePropertyId, type, &condition)) &&
        condition && SUCCEEDED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &buttons)) && buttons) {
        int count = 0;
        IUIAutomationElementArray_get_Length(buttons, &count);
        for (int i = 0; i < count && !found; ++i) {
            IUIAutomationElement* button = NULL;
            BSTR id = NULL, name = NULL;
            BOOL offscreen = TRUE;
            RECT candidate;
            if (FAILED(IUIAutomationElementArray_GetElement(buttons, i, &button)) || !button) continue;
            IUIAutomationElement_get_CurrentAutomationId(button, &id);
            IUIAutomationElement_get_CurrentName(button, &name);
            IUIAutomationElement_get_CurrentIsOffscreen(button, &offscreen);
            if (!offscreen && ((id && !lstrcmpiW(id, L"StartButton")) || StartName(name)) &&
                SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(button, &candidate)) &&
                IsReasonableStartRect(&candidate, taskbar)) {
                *rect = candidate; found = TRUE;
            }
            SysFreeString(id); SysFreeString(name);
            IUIAutomationElement_Release(button);
        }
    }
    if (buttons) IUIAutomationElementArray_Release(buttons);
    if (condition) IUIAutomationCondition_Release(condition);
    if (root) IUIAutomationElement_Release(root);
    IUIAutomation_Release(automation);
    return found;
}

static BOOL StartRectAccessible(IAccessible* object, VARIANT child, const RECT* taskbar, RECT* rect) {
    BSTR name = NULL;
    VARIANT role, state;
    LONG x, y, width, height;
    BOOL found = FALSE;
    VariantInit(&role); VariantInit(&state);
    IAccessible_get_accName(object, child, &name);
    IAccessible_get_accRole(object, child, &role);
    IAccessible_get_accState(object, child, &state);
    if (StartName(name) && role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_PUSHBUTTON &&
        state.vt == VT_I4 && !(state.lVal & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) &&
        SUCCEEDED(IAccessible_accLocation(object, &x, &y, &width, &height, child)) &&
        width > 0 && height > 0 && (long long)x + width <= LONG_MAX && (long long)y + height <= LONG_MAX) {
        RECT candidate = {x, y, x + width, y + height};
        if (IsReasonableStartRect(&candidate, taskbar)) { *rect = candidate; found = TRUE; }
    }
    SysFreeString(name); VariantClear(&role); VariantClear(&state);
    return found;
}

static BOOL WalkAccessible(IAccessible* object, const RECT* taskbar, RECT* rect, int depth, int* budget) {
    VARIANT self;
    LONG count = 0;
    VariantInit(&self); self.vt = VT_I4; self.lVal = CHILDID_SELF;
    if (--*budget < 0 || depth > 12) return FALSE;
    if (StartRectAccessible(object, self, taskbar, rect)) return TRUE;
    if (FAILED(IAccessible_get_accChildCount(object, &count))) return FALSE;
    for (LONG i = 0; i < count && *budget > 0; ++i) {
        VARIANT child;
        LONG fetched = 0;
        BOOL found = FALSE;
        VariantInit(&child);
        if (SUCCEEDED(AccessibleChildren(object, i, 1, &child, &fetched)) && fetched == 1) {
            if (child.vt == VT_DISPATCH && child.pdispVal) {
                IAccessible* nested = NULL;
                if (SUCCEEDED(IDispatch_QueryInterface(child.pdispVal, &IID_IAccessible, (void**)&nested)) && nested) {
                    found = WalkAccessible(nested, taskbar, rect, depth + 1, budget);
                    IAccessible_Release(nested);
                }
            } else if (child.vt == VT_I4) {
                --*budget;
                found = StartRectAccessible(object, child, taskbar, rect);
            }
        }
        VariantClear(&child);
        if (found) return TRUE;
    }
    return FALSE;
}

static BOOL StartRectMSAA(const RECT* taskbar, RECT* rect) {
    IAccessible* root = NULL;
    BOOL found = FALSE;
    int budget = 256;
    if (SUCCEEDED(AccessibleObjectFromWindow(g_hTaskbar, OBJID_CLIENT, &IID_IAccessible, (void**)&root)) && root) {
        found = WalkAccessible(root, taskbar, rect, 0, &budget);
        IAccessible_Release(root);
    }
    return found;
}

static BOOL StartRectWindow(const RECT* taskbar, RECT* rect) {
    return g_hStartButton && g_hStartButton != g_hTaskbar &&
           IsChild(g_hTaskbar, g_hStartButton) && IsWindowVisible(g_hStartButton) &&
           GetWindowRect(g_hStartButton, rect) && IsReasonableStartRect(rect, taskbar);
}

static BOOL RefreshTaskbarHandles(void) {
    g_hTaskbar = FindTaskbarWindow();
    g_hStartButton = g_hTaskbar ? FindStartButton(g_hTaskbar) : NULL;
    return g_hTaskbar != NULL;
}

// One detector thread for the process lifetime. Explorer/UIA may be slow; the
// input hook only reads the last published snapshot and never waits for it.
static DWORD WINAPI DetectStartRects(void* unused) {
    UNREFERENCED_PARAMETER(unused);
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    for (;;) {
        RECT taskbar = {0}, start = {0};
        BOOL legacy = InterlockedCompareExchange(&g_detectorLegacy, 0, 0) != 0;
        BOOL found = FALSE;
        if (RefreshTaskbarHandles() && IsWindowVisible(g_hTaskbar) &&
            GetWindowRect(g_hTaskbar, &taskbar)) {
            if (!legacy && (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE))
                found = StartRectUIA(&taskbar, &start) || StartRectMSAA(&taskbar, &start);
            if (!found) found = StartRectWindow(&taskbar, &start);
        }
        AcquireSRWLockExclusive(&g_startCacheLock);
        g_cachedStart = start;
        g_cachedTaskbar = taskbar;
        g_cachedTaskbarWindow = found ? g_hTaskbar : NULL;
        g_cachedLegacy = legacy;
        g_cachedAt = GetTickCount64();
        ReleaseSRWLockExclusive(&g_startCacheLock);
        Sleep(500);
    }
}

static BOOL IsStartButtonClickPoint(POINT pt) {
    RECT taskbar, detected, final;
    HWND taskbarWindow;
    BOOL legacy;
    ULONGLONG stamp;
    InterlockedExchange(&g_detectorLegacy, g_cfg.legacyStartButtonDetection);
    if (!TryAcquireSRWLockShared(&g_startCacheLock)) return FALSE;
    taskbar = g_cachedTaskbar;
    detected = g_cachedStart;
    taskbarWindow = g_cachedTaskbarWindow;
    legacy = g_cachedLegacy;
    stamp = g_cachedAt;
    ReleaseSRWLockShared(&g_startCacheLock);
    if (!taskbarWindow || legacy != g_cfg.legacyStartButtonDetection ||
        GetTickCount64() - stamp > 2000 || !PtInRect(&taskbar, pt)) return FALSE;
    HWND hit = WindowFromPoint(pt);
    if (hit != taskbarWindow && !IsChild(taskbarWindow, hit)) return FALSE;
    return start_click_rect(&detected, &taskbar, g_cfg.startClickWidth,
        g_cfg.startClickHeight, g_cfg.startClickOffsetX, g_cfg.startClickOffsetY, &final) &&
        PtInRect(&final, pt);
}
void SetTaskbarHookTargetWindow(HWND hWnd) {
    g_hOwnerWnd = hWnd;
}

// Low-level mouse hook procedure to intercept start button clicks globally
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (wParam == WM_LBUTTONUP && g_suppressNextLeftUp) {
            g_suppressNextLeftUp = FALSE;
            return 1;
        }
        if (wParam == WM_MBUTTONUP && g_suppressNextMiddleUp) {
            g_suppressNextMiddleUp = FALSE;
            return 1;
        }
        if (wParam == WM_RBUTTONUP && g_suppressNextRightUp) {
            g_suppressNextRightUp = FALSE;
            return 1;
        }

        MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
        
        // Check for mouse clicks
        BOOL bLeftClick = (wParam == WM_LBUTTONDOWN);
        BOOL bMiddleClick = (wParam == WM_MBUTTONDOWN);
        BOOL bRightClick = (wParam == WM_RBUTTONDOWN);
        
        if (bLeftClick || bMiddleClick || bRightClick) {
            if (should_block_triggers_for_fullscreen(g_cfg.ignoreTriggersWhenFullscreen, g_cfg.fullscreenExclusionList, g_hOwnerWnd)) {
                return CallNextHookEx(g_hMsgHook, nCode, wParam, lParam);
            }

            // Check if click is over start button
            if (IsStartButtonClickPoint(pMouse->pt)) {
                
                WCHAR debug[256];
                wsprintfW(debug, L"Start button clicked at (%d,%d): Left=%d, Middle=%d, Right=%d\n", 
                         pMouse->pt.x, pMouse->pt.y, bLeftClick, bMiddleClick, bRightClick);
                OutputDebugStringW(debug);
                
                ControlActionType action = CA_NOTHING;
                BOOL shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                MenuTriggerType trigger = MENU_TRIGGER_OTHER;
                
                if (bLeftClick) {
                    if ((!shiftPressed && g_cfg.leftClickTrigger) || (shiftPressed && g_cfg.shiftLeftClickTrigger)) {
                        action = g_cfg.leftClickAction;
                    }
                } else if (bMiddleClick) {
                    if ((!shiftPressed && g_cfg.middleClickTrigger) || (shiftPressed && g_cfg.shiftMiddleClickTrigger)) {
                        action = g_cfg.leftClickAction;
                    }
                } else if (bRightClick) {
                    if ((!shiftPressed && g_cfg.rightClickTrigger) || (shiftPressed && g_cfg.shiftRightClickTrigger)) {
                        action = g_cfg.leftClickAction;
                    }
                }

                if (shiftPressed) trigger = MENU_TRIGGER_SHIFT;
                else if (bRightClick) trigger = MENU_TRIGGER_RIGHT_CLICK;
                else if (bMiddleClick) trigger = MENU_TRIGGER_MIDDLE_CLICK;
                
                if (action != CA_NOTHING) {
                    HWND hTarget = g_hOwnerWnd;
                    if (!hTarget) return CallNextHookEx(g_hMsgHook, nCode, wParam, lParam);

                    // Never launch the menu synchronously from a low-level hook.
                    // WinUI bridge startup can take long enough for Windows to
                    // punish the hook and leave mouse state feeling stuck.
                    if (action == CA_WINMAC_MENU && hTarget) {
                        AllowSetForegroundWindow(ASFW_ANY);
                        PostMessageW(hTarget, WM_APP, (WPARAM)trigger, 0);
                    } else {
                        PostMessageW(hTarget, WM_APP + 0x61, (WPARAM)trigger, 0);
                    }
                    
                    // Suppress the click if we're not showing Windows menu
                    if (action != CA_WINDOWS_MENU) {
                        if (bLeftClick) g_suppressNextLeftUp = TRUE;
                        if (bMiddleClick) g_suppressNextMiddleUp = TRUE;
                        if (bRightClick) g_suppressNextRightUp = TRUE;
                        return 1; // Suppress the mouse event
                    }
                }
            }
        }
    }
    
    return CallNextHookEx(g_hMsgHook, nCode, wParam, lParam);
}

HWND FindTaskbarWindow(void) {
    return FindWindow(L"Shell_TrayWnd", NULL);
}

HWND FindStartButton(HWND hTaskbar) {
    if (!hTaskbar) return NULL;
    
    // Try different methods to find the start button depending on Windows version
    HWND hStartButton = NULL;
    
    // Method 1: Direct child search for "Start" button
    hStartButton = FindWindowEx(hTaskbar, NULL, L"Start", NULL);
    if (hStartButton) return hStartButton;
    
    // Method 2: Look for the start button in the tray area
    HWND hTray = FindWindowEx(hTaskbar, NULL, L"TrayNotifyWnd", NULL);
    if (!hTray) {
        // Try ReBarWindow32 for newer Windows versions
        HWND hRebar = FindWindowEx(hTaskbar, NULL, L"ReBarWindow32", NULL);
        if (hRebar) {
            // Look for MSTaskSwWClass 
            HWND hTaskSwitch = FindWindowEx(hRebar, NULL, L"MSTaskSwWClass", NULL);
            if (hTaskSwitch) {
                // Start button might be a sibling or child
                hStartButton = FindWindowEx(hRebar, NULL, L"Start", NULL);
                if (hStartButton) return hStartButton;
            }
        }
    }
    
    return hStartButton;
}

BOOL InitTaskbarHook(void) {
    OutputDebugStringW(L"InitTaskbarHook: Starting taskbar hook initialization\n");

    InterlockedExchange(&g_detectorLegacy, g_cfg.legacyStartButtonDetection);
    if (InterlockedCompareExchange(&g_detectorStarted, 1, 0) == 0) {
        HANDLE detector = CreateThread(NULL, 0, DetectStartRects, NULL, 0, NULL);
        if (!detector) { InterlockedExchange(&g_detectorStarted, 0); return FALSE; }
        CloseHandle(detector); // detector persists across Explorer/hook restarts
    }

    // Install low-level mouse hook to intercept mouse messages globally
    if (!g_hMsgHook) {
        g_hMsgHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);
        if (!g_hMsgHook) {
            OutputDebugStringW(L"InitTaskbarHook: Failed to install mouse hook\n");
            return FALSE;
        }
        OutputDebugStringW(L"InitTaskbarHook: Mouse hook installed successfully\n");
    } else {
        OutputDebugStringW(L"InitTaskbarHook: Mouse hook already installed, refreshed handles only\n");
    }

    return TRUE;
}

void ShutdownTaskbarHook(void) {
    if (g_hMsgHook) {
        UnhookWindowsHookEx(g_hMsgHook);
        g_hMsgHook = NULL;
        OutputDebugStringW(L"ShutdownTaskbarHook: Mouse hook removed\n");
    }
    
    AcquireSRWLockExclusive(&g_startCacheLock);
    g_cachedTaskbarWindow = NULL;
    ReleaseSRWLockExclusive(&g_startCacheLock);
    g_hOwnerWnd = NULL;
    g_suppressNextLeftUp = FALSE;
    g_suppressNextMiddleUp = FALSE;
    g_suppressNextRightUp = FALSE;
}
