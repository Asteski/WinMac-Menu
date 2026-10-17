#pragma once
#include <windows.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

BOOL LaunchWinUI3Menu(const Config* cfg, MenuTriggerType trigger);
void SetWinUI3OwnerWindow(HWND owner);
BOOL PollWinUI3Menu(UINT_PTR timerId);
BOOL LaunchWinUI3MenuAndWait(const Config* cfg, MenuTriggerType trigger, DWORD timeoutMs);
BOOL PreloadWinUI3Menu(const Config* cfg);
BOOL RefreshWinUI3MenuConfig(const Config* cfg);
void ShutdownWinUI3Menu(void);

#ifdef __cplusplus
}
#endif
