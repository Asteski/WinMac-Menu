#pragma once
#include <windows.h>

/* Manual check only. Opens GitHub in the browser after an explicit button click. */
void ShowUpdateCheck(HWND owner);
BOOL update_parse_version(const char* text, unsigned version[3]);
int update_compare_versions(const unsigned left[3], const unsigned right[3]);
BOOL update_release_tag(const char* json, char* tag, size_t capacity);
