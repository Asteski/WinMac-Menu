#pragma once
#include <windows.h>
#include <stdlib.h>

// Resources owned by one native menu opening. Borrowed window/class icons must
// be copied before registration; shared process-wide icons are never registered.
typedef enum MenuResourceKind { MENU_MEMORY, MENU_ICON, MENU_BITMAP } MenuResourceKind;
typedef struct MenuResource {
    struct MenuResource* next;
    HANDLE handle;
    MenuResourceKind kind;
} MenuResource;
static MenuResource* menuResources;

static void menu_resource_destroy(HANDLE handle, MenuResourceKind kind) {
    if (kind == MENU_MEMORY) LocalFree(handle);
    else if (kind == MENU_ICON) DestroyIcon((HICON)handle);
    else DeleteObject(handle);
}
static HANDLE menu_resource_own(HANDLE handle, MenuResourceKind kind) {
    if (!handle) return NULL;
    for (MenuResource* r = menuResources; r; r = r->next)
        if (r->handle == handle && r->kind == kind) return handle;
    MenuResource* r = (MenuResource*)malloc(sizeof(*r));
    if (!r) { menu_resource_destroy(handle, kind); return NULL; }
    *r = (MenuResource){ menuResources, handle, kind };
    menuResources = r;
    return handle;
}
static void menu_resource_release(HANDLE handle, MenuResourceKind kind) {
    if (!handle) return;
    MenuResource** link = &menuResources;
    while (*link) {
        MenuResource* r = *link;
        if (r->handle == handle && r->kind == kind) {
            *link = r->next;
            free(r);
            break;
        }
        link = &r->next;
    }
    menu_resource_destroy(handle, kind);
}
static HLOCAL menu_alloc(UINT flags, SIZE_T bytes) {
    return (HLOCAL)menu_resource_own(LocalAlloc(flags, bytes), MENU_MEMORY);
}
static void menu_free(HLOCAL memory) { menu_resource_release(memory, MENU_MEMORY); }
static HICON menu_own_icon(HICON icon) { return (HICON)menu_resource_own(icon, MENU_ICON); }
static void menu_release_icon(HICON icon) { menu_resource_release(icon, MENU_ICON); }
static void menu_resources_clear(void) {
    while (menuResources) {
        MenuResource* r = menuResources;
        menuResources = r->next;
        menu_resource_destroy(r->handle, r->kind);
        free(r);
    }
}
