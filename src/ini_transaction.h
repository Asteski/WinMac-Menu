#pragma once
#include <windows.h>
#include <shlwapi.h>

typedef struct IniTransaction {
    WCHAR target[MAX_PATH];
    WCHAR temporary[MAX_PATH];
    DWORD error;
} IniTransaction;

static BOOL ini_transaction_begin(IniTransaction* tx, const WCHAR* target) {
    ZeroMemory(tx, sizeof(*tx));
    lstrcpynW(tx->target, target, ARRAYSIZE(tx->target));
    WCHAR folder[MAX_PATH];
    lstrcpynW(folder, target, ARRAYSIZE(folder));
    if (!PathRemoveFileSpecW(folder) || !GetTempFileNameW(folder, L"wmm", 0, tx->temporary)) return FALSE;
    if (!CopyFileW(target, tx->temporary, FALSE)) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            DeleteFileW(tx->temporary);
            tx->temporary[0] = 0;
            SetLastError(error);
            return FALSE;
        }
    }
    return TRUE;
}
static BOOL ini_transaction_write(IniTransaction* tx, const WCHAR* section,
    const WCHAR* key, const WCHAR* value, const WCHAR* path) {
    if (WritePrivateProfileStringW(section, key, value, path)) return TRUE;
    if (!tx->error) tx->error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    return FALSE;
}
static BOOL ini_transaction_commit(IniTransaction* tx) {
    if (tx->error) { SetLastError(tx->error); return FALSE; }
    // The profile API returns zero for cache flushes, including successful ones.
    WritePrivateProfileStringW(NULL, NULL, NULL, tx->temporary);
    HANDLE file = CreateFileW(tx->temporary, GENERIC_WRITE, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    BOOL flushed = FlushFileBuffers(file);
    DWORD error = GetLastError();
    CloseHandle(file);
    if (!flushed) { SetLastError(error); return FALSE; }
    return MoveFileExW(tx->temporary, tx->target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
static void ini_transaction_discard(IniTransaction* tx) {
    if (tx->temporary[0]) {
        // This exact temporary file was created by begin(), beside the target INI.
        SetFileAttributesW(tx->temporary, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(tx->temporary);
        tx->temporary[0] = 0;
    }
}
