#include "recent.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <initguid.h>
#include <wctype.h>

static BOOL recent_target_is_protocol_shortcut(const WCHAR *target) {
    const WCHAR *colon;
    const WCHAR *p;

    if (!target || !target[0]) return FALSE;
    while (*target == L' ' || *target == L'\t') target++;
    colon = wcschr(target, L':');
    if (!colon || colon == target) return FALSE;

    if (colon == target + 1 && iswalpha(target[0]) &&
        (target[2] == L'\\' || target[2] == L'/')) {
        return FALSE;
    }

    if (!iswalpha(target[0])) return FALSE;
    for (p = target + 1; p < colon; ++p) {
        if (!iswalnum(*p) && *p != L'+' && *p != L'-' && *p != L'.') return FALSE;
    }
    return TRUE;
}

static int add_from_jump_list(RecentItem **out, int maxItems, BOOL hideProtocolShortcuts) {
    // Basic approach: enumerate Recent Items folder
    // %AppData%\Microsoft\Windows\Recent
    WCHAR recentPath[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_RECENT, NULL, SHGFP_TYPE_CURRENT, recentPath))) return 0;

    WIN32_FIND_DATAW fd; WCHAR pattern[MAX_PATH];
    PathCombineW(pattern, recentPath, L"*.lnk");
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    RecentItem *items = (RecentItem*)LocalAlloc(LMEM_ZEROINIT, sizeof(RecentItem) * maxItems);
    if (!items) { FindClose(h); return 0; }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (count >= maxItems) break;
        WCHAR full[MAX_PATH]; PathCombineW(full, recentPath, fd.cFileName);
        // Resolve shortcut target
        IShellLinkW *psl = NULL; HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&psl);
        if (SUCCEEDED(hr)) {
            IPersistFile *ppf; if (SUCCEEDED(IShellLinkW_QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
                if (SUCCEEDED(IPersistFile_Load(ppf, full, STGM_READ))) {
                    WIN32_FIND_DATAW wfd; WCHAR target[MAX_PATH];
                    if (SUCCEEDED(IShellLinkW_GetPath(psl, target, MAX_PATH, &wfd, SLGP_RAWPATH))) {
                        if (target[0]) {
                            DWORD attrs = GetFileAttributesW(target);
                            BOOL isProtocolShortcut = recent_target_is_protocol_shortcut(target);
                            // With protocol filtering enabled, accept only targets that
                            // resolve to a real filesystem item. This covers unknown URI
                            // schemes such as gbtdllib: even when ShellLink exposes them
                            // in an unusual form.
                            if (hideProtocolShortcuts && attrs == INVALID_FILE_ATTRIBUTES) {
                                // Intentionally hidden by user setting.
                            } else if (attrs != INVALID_FILE_ATTRIBUTES || isProtocolShortcut) {
                                lstrcpynW(items[count].path, target, MAX_PATH);
                                items[count].isFolder = attrs != INVALID_FILE_ATTRIBUTES &&
                                    ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || (attrs & FILE_ATTRIBUTE_DIRECTORY));
                                count++;
                            }
                        }
                    }
                }
                IPersistFile_Release(ppf);
            }
            IShellLinkW_Release(psl);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    *out = items;
    return count;
}

int recent_get_items(RecentItem **list, int maxItems, BOOL hideProtocolShortcuts) {
    *list = NULL;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int n = add_from_jump_list(list, maxItems, hideProtocolShortcuts);
    CoUninitialize();
    return n;
}

void recent_open_item(const RecentItem *item) {
    if (!item) return;
    if (!item->path[0]) return;
    DWORD attrs = GetFileAttributesW(item->path);
    if (attrs == INVALID_FILE_ATTRIBUTES && !recent_target_is_protocol_shortcut(item->path)) return; // silently ignore stale entry
    ShellExecuteW(NULL, L"open", item->path, NULL, NULL, SW_SHOWNORMAL);
}

void recent_open_parent_folder(const RecentItem *item) {
    if (!item) return;
    if (!item->path[0]) return;
    if (recent_target_is_protocol_shortcut(item->path)) return;
    DWORD attrs = GetFileAttributesW(item->path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return; // silently ignore stale entry
    
    WCHAR parentPath[MAX_PATH];
    lstrcpynW(parentPath, item->path, ARRAYSIZE(parentPath));
    
    // If it's a file, get its directory
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        WCHAR* lastSlash = wcsrchr(parentPath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
        }
    }
    // If it's a directory, open it as-is (it's already the root folder)
    
    // Open the parent folder and select the item if it was a file
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        // For files, use /select to highlight the file in the folder
        WCHAR cmdLine[MAX_PATH * 2];
        wsprintfW(cmdLine, L"/select,%s", item->path);
        ShellExecuteW(NULL, L"open", L"explorer.exe", cmdLine, NULL, SW_SHOWNORMAL);
    } else {
        // For folders, just open the folder
        ShellExecuteW(NULL, L"open", parentPath, NULL, NULL, SW_SHOWNORMAL);
    }
}

void recent_clear_all(void) {
    WCHAR recentPath[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_RECENT, NULL, SHGFP_TYPE_CURRENT, recentPath))) return;
    WIN32_FIND_DATAW fd; WCHAR pattern[MAX_PATH];
    PathCombineW(pattern, recentPath, L"*.lnk");
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        WCHAR full[MAX_PATH]; PathCombineW(full, recentPath, fd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
