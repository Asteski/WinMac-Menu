#include "update_check.h"
#include <commctrl.h>
#include <winhttp.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE_PAGE L"https://github.com/Asteski/WinMac-Menu/releases/latest"
#define RELEASE_API L"/repos/Asteski/WinMac-Menu/releases/latest"
#define RESPONSE_LIMIT (1024 * 1024)

BOOL update_parse_version(const char* text, unsigned version[3]) {
    unsigned parsed[3] = {0};
    if (!text) return FALSE;
    if (*text == 'v' || *text == 'V') ++text;
    for (int i = 0; i < 3; ++i) {
        if (*text < '0' || *text > '9') return FALSE;
        while (*text >= '0' && *text <= '9') {
            parsed[i] = parsed[i] * 10 + (*text++ - '0');
            if (parsed[i] > 65535) return FALSE;
        }
        if (!*text && i == 2) { memcpy(version, parsed, sizeof(parsed)); return TRUE; }
        if (*text++ != '.') return FALSE;
    }
    return FALSE;
}

int update_compare_versions(const unsigned left[3], const unsigned right[3]) {
    for (int i = 0; i < 3; ++i)
        if (left[i] != right[i]) return left[i] > right[i] ? 1 : -1;
    return 0;
}

/* Small bounded JSON reader. Only the root tag_name is used; URLs/content from
   the response are never executed or presented as instructions. */
typedef struct JsonReader { const char* p; char* tag; size_t capacity; BOOL seen, unstable; } JsonReader;
static void json_space(JsonReader* r) { while (*r->p && strchr(" \r\n\t", *r->p)) ++r->p; }
static BOOL json_string(JsonReader* r, char* out, size_t capacity) {
    size_t n = 0;
    if (*r->p++ != '"') return FALSE;
    while (*r->p && *r->p != '"') {
        unsigned char ch = (unsigned char)*r->p++;
        if (ch < 32) return FALSE;
        if (ch == '\\') {
            ch = (unsigned char)*r->p++;
            if (!ch) return FALSE;
            if (ch == 'u') {
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = *r->p;
                    int digit = h >= '0' && h <= '9' ? h-'0' :
                        h >= 'a' && h <= 'f' ? h-'a'+10 : h >= 'A' && h <= 'F' ? h-'A'+10 : -1;
                    if (digit < 0) return FALSE;
                    ++r->p; value = value * 16 + digit;
                }
                ch = value > 0 && value < 128 ? (unsigned char)value : '?';
            } else if (ch == 'b') ch = '\b';
            else if (ch == 'f') ch = '\f';
            else if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (!strchr("\"\\/", ch)) return FALSE;
        }
        if (out) { if (n + 1 >= capacity) return FALSE; out[n] = (char)ch; }
        ++n;
    }
    if (*r->p != '"') return FALSE;
    ++r->p;
    if (out) out[n] = 0;
    return TRUE;
}

static BOOL json_value(JsonReader* r, int depth) {
    if (depth > 32) return FALSE;
    json_space(r);
    char opening = *r->p;
    if (opening == '"') return json_string(r, NULL, 0);
    if (opening == '{' || opening == '[') {
        char closing = opening == '{' ? '}' : ']';
        ++r->p; json_space(r);
        if (*r->p == closing) { ++r->p; return TRUE; }
        for (;;) {
            BOOL tag = FALSE;
            if (opening == '{') {
                char key[128];
                if (!json_string(r, key, sizeof(key))) return FALSE;
                json_space(r);
                if (*r->p++ != ':') return FALSE;
                json_space(r);
                tag = depth == 0 && !strcmp(key, "tag_name");
                if (depth == 0 && (!strcmp(key, "draft") || !strcmp(key, "prerelease")) &&
                    !strncmp(r->p, "true", 4)) r->unstable = TRUE;
            }
            if (tag) {
                if (r->seen || !json_string(r, r->tag, r->capacity)) return FALSE;
                r->seen = TRUE;
            } else if (!json_value(r, depth + 1)) return FALSE;
            json_space(r);
            if (*r->p == closing) { ++r->p; return TRUE; }
            if (*r->p++ != ',') return FALSE;
            json_space(r);
        }
    }
    const char* words[] = {"true", "false", "null"};
    for (int i = 0; i < 3; ++i) {
        size_t n = strlen(words[i]);
        if (!strncmp(r->p, words[i], n)) { r->p += n; return TRUE; }
    }
    if (*r->p == '-') ++r->p;
    if (*r->p == '0') ++r->p;
    else {
        if (*r->p < '1' || *r->p > '9') return FALSE;
        while (*r->p >= '0' && *r->p <= '9') ++r->p;
    }
    if (*r->p == '.') {
        ++r->p;
        if (*r->p < '0' || *r->p > '9') return FALSE;
        while (*r->p >= '0' && *r->p <= '9') ++r->p;
    }
    if (*r->p == 'e' || *r->p == 'E') {
        ++r->p; if (*r->p == '+' || *r->p == '-') ++r->p;
        if (*r->p < '0' || *r->p > '9') return FALSE;
        while (*r->p >= '0' && *r->p <= '9') ++r->p;
    }
    return TRUE;
}

BOOL update_release_tag(const char* json, char* tag, size_t capacity) {
    if (!json || !tag || !capacity) return FALSE;
    JsonReader reader = {json, tag, capacity, FALSE, FALSE};
    json_space(&reader);
    if (*reader.p != '{' || !json_value(&reader, 0)) return FALSE;
    json_space(&reader);
    return !*reader.p && reader.seen && !reader.unstable;
}

typedef enum UpdateStatus { UPDATE_ERROR, UPDATE_NONE, UPDATE_NEW, UPDATE_CURRENT, UPDATE_LIMITED, UPDATE_INVALID } UpdateStatus;
typedef struct UpdateJob {
    LONG refs, done, cancelled;
    UpdateStatus status;
    unsigned installed[3];
    WCHAR installedText[64], latestText[64];
} UpdateJob;

static void release_job(UpdateJob* job) { if (!InterlockedDecrement(&job->refs)) free(job); }

static BOOL installed_version(UpdateJob* job) {
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, ARRAYSIZE(path))) return FALSE;
    DWORD size = GetFileVersionInfoSizeW(path, NULL);
    void* data = size ? malloc(size) : NULL;
    VS_FIXEDFILEINFO* version = NULL;
    UINT length = 0;
    BOOL ok = data && GetFileVersionInfoW(path, 0, size, data) &&
        VerQueryValueW(data, L"\\", (void**)&version, &length) && length >= sizeof(*version) && version->dwSignature == 0xFEEF04BD;
    if (ok) {
        job->installed[0] = HIWORD(version->dwProductVersionMS);
        job->installed[1] = LOWORD(version->dwProductVersionMS);
        job->installed[2] = HIWORD(version->dwProductVersionLS);
        swprintf_s(job->installedText, ARRAYSIZE(job->installedText), L"%u.%u.%u",
                   job->installed[0], job->installed[1], job->installed[2]);
    }
    free(data);
    return ok;
}

static char* fetch_release(UpdateJob* job) {
    char* body = calloc(RESPONSE_LIMIT + 1, 1);
    if (!body) return NULL;
#ifdef WINMAC_UPDATE_TEST_FILE
    FILE* file = NULL;
    if (fopen_s(&file, WINMAC_UPDATE_TEST_FILE, "rb") || !file) { free(body); return NULL; }
    size_t size = fread(body, 1, RESPONSE_LIMIT, file);
    BOOL ok = !ferror(file) && fgetc(file) == EOF;
    fclose(file);
    if (!ok || memchr(body, 0, size)) { free(body); return NULL; }
    return body;
#else
    HINTERNET session = WinHttpOpen(L"WinMacMenu Update Check", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    HINTERNET connection = NULL, request = NULL;
    BOOL ok = FALSE;
    if (!session) goto cleanup;
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) goto cleanup;
    request = WinHttpOpenRequest(connection, L"GET", RELEASE_API, NULL, NULL, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) goto cleanup;
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (!WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", (DWORD)-1,
                            NULL, 0, 0, 0) || !WinHttpReceiveResponse(request, NULL)) goto cleanup;
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &bytes, NULL)) goto cleanup;
    if (status == 404) { job->status = UPDATE_NONE; goto cleanup; }
    if (status == 403 || status == 429) { job->status = UPDATE_LIMITED; goto cleanup; }
    if (status != 200) goto cleanup;
    DWORD total = 0;
    ULONGLONG deadline = GetTickCount64() + 15000;
    for (;;) {
        DWORD received = 0;
        if (InterlockedCompareExchange(&job->cancelled, 0, 0) || GetTickCount64() > deadline) goto cleanup;
        if (total == RESPONSE_LIMIT || !WinHttpReadData(request, body + total, min(8192, RESPONSE_LIMIT - total), &received)) goto cleanup;
        if (!received) break;
        if (memchr(body + total, 0, received)) goto cleanup;
        total += received;
    }
    ok = TRUE;
cleanup:
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    if (!ok) { free(body); return NULL; }
    return body;
#endif
}

static DWORD WINAPI update_worker(void* parameter) {
    UpdateJob* job = parameter;
    char* body = fetch_release(job);
    if (body) {
        char tag[64]; unsigned latest[3];
        if (update_release_tag(body, tag, sizeof(tag)) && update_parse_version(tag, latest)) {
            MultiByteToWideChar(CP_UTF8, 0, tag, -1, job->latestText, ARRAYSIZE(job->latestText));
            job->status = update_compare_versions(latest, job->installed) > 0 ? UPDATE_NEW : UPDATE_CURRENT;
        } else job->status = UPDATE_INVALID;
        free(body);
    }
    InterlockedExchange(&job->done, 1);
    release_job(job);
    return 0;
}

static HRESULT CALLBACK update_dialog_callback(HWND dialog, UINT message, WPARAM wp, LPARAM lp, LONG_PTR data) {
    UpdateJob* job = (UpdateJob*)data;
    if (message == TDN_CREATED) {
        ShowWindow(GetDlgItem(dialog, IDOK), SW_HIDE);
        SendMessageW(dialog, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 30);
    }
    if (message == TDN_TIMER && InterlockedCompareExchange(&job->done, 0, 0))
        SendMessageW(dialog, TDM_CLICK_BUTTON, IDOK, 0);
    if (message == TDN_BUTTON_CLICKED && wp == IDCANCEL) InterlockedExchange(&job->cancelled, 1);
    if (message == TDN_BUTTON_CLICKED && wp == IDOK && !InterlockedCompareExchange(&job->done, 0, 0)) return S_FALSE;
    return S_OK;
}

void ShowUpdateCheck(HWND owner) {
    UpdateJob* job = calloc(1, sizeof(*job));
    if (!job) return;
    job->refs = 2;
    if (!installed_version(job)) {
        free(job);
        MessageBoxW(owner, L"Could not read the installed application version.", L"Check for updates", MB_OK | MB_ICONERROR);
        return;
    }
    HANDLE worker = CreateThread(NULL, 0, update_worker, job, 0, NULL);
    if (!worker) { free(job); return; }
    CloseHandle(worker);
    TASKDIALOGCONFIG dialog = {sizeof(dialog)};
    dialog.hwndParent = owner;
#ifdef WINMAC_UPDATE_TEST_FILE
    dialog.pszWindowTitle = L"Check for updates (test fixture)";
#else
    dialog.pszWindowTitle = L"Check for updates";
#endif
    dialog.pszMainInstruction = L"Checking for updates...";
    dialog.pszContent = L"Looking for the latest stable WinMac Menu release on GitHub.";
    dialog.dwFlags = TDF_CALLBACK_TIMER | TDF_SHOW_MARQUEE_PROGRESS_BAR | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON | TDCBF_OK_BUTTON;
    dialog.nDefaultButton = IDCANCEL;
    dialog.pfCallback = update_dialog_callback;
    dialog.lpCallbackData = (LONG_PTR)job;
    int clicked = IDCANCEL;
    HRESULT result = TaskDialogIndirect(&dialog, &clicked, NULL, NULL);
    if (FAILED(result) || clicked == IDCANCEL || !InterlockedCompareExchange(&job->done, 0, 0)) {
        InterlockedExchange(&job->cancelled, 1);
        release_job(job);
        return;
    }
    WCHAR content[512];
    const WCHAR* title;
    switch (job->status) {
    case UPDATE_NEW:
        title = L"A new version is available";
        swprintf_s(content, ARRAYSIZE(content), L"Installed: %s\nLatest release: %s\n\nOpen GitHub to view release notes and choose an installer or portable ZIP.", job->installedText, job->latestText);
        break;
    case UPDATE_CURRENT:
        title = L"You're up to date";
        swprintf_s(content, ARRAYSIZE(content), L"Installed: %s\nLatest stable release: %s", job->installedText, job->latestText);
        break;
    case UPDATE_NONE:
        title = L"No public stable release found";
        wcscpy_s(content, ARRAYSIZE(content), L"GitHub has no public latest release available for Asteski/WinMac-Menu yet.");
        break;
    case UPDATE_LIMITED:
        title = L"GitHub declined the update check";
        wcscpy_s(content, ARRAYSIZE(content), L"The API may be rate-limited or access may be restricted. Try again later or open the releases page.");
        break;
    case UPDATE_INVALID:
        title = L"Couldn't compare release versions";
        wcscpy_s(content, ARRAYSIZE(content), L"The release response or version tag was not recognized. You can check the releases page manually.");
        break;
    default:
        title = L"Couldn't check for updates";
        wcscpy_s(content, ARRAYSIZE(content), L"Check your internet connection and try again, or open the releases page manually.");
    }
    TASKDIALOG_BUTTON buttons[] = {{100, L"Open GitHub releases"}};
    TASKDIALOGCONFIG completed = {sizeof(completed)};
    completed.hwndParent = owner;
    completed.pszWindowTitle = dialog.pszWindowTitle;
    completed.pszMainInstruction = title;
    completed.pszContent = content;
    completed.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    completed.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    completed.cButtons = 1; completed.pButtons = buttons; completed.nDefaultButton = IDCLOSE;
    clicked = IDCLOSE;
    if (SUCCEEDED(TaskDialogIndirect(&completed, &clicked, NULL, NULL)) && clicked == 100)
        ShellExecuteW(owner, L"open", RELEASE_PAGE, NULL, NULL, SW_SHOWNORMAL);
    release_job(job);
}
