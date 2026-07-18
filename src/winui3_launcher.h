#pragma once
#include <windows.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

BOOL LaunchWinUI3Menu(const Config* cfg, MenuTriggerType trigger);
BOOL PreloadWinUI3Menu(const Config* cfg);
void ShutdownWinUI3Menu(void);

#ifdef __cplusplus
}
#endif
