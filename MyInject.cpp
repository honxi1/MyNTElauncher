// MyInject.cpp - 注入载体DLL（精简版，对齐小飞二进制）
#include <windows.h>
#include <string.h>
#define _STRICMP _stricmp

#pragma section("Shared", read, write, shared)
__attribute__((section("Shared"), shared)) volatile LONG   g_injectDone   = 0;
__attribute__((section("Shared"), shared)) volatile DWORD  g_injectDelay  = 0;
__attribute__((section("Shared"), shared)) volatile LONG   g_needInject   = 0;
__attribute__((section("Shared"), shared)) volatile LONG   g_injectStarted = 0;
__attribute__((section("Shared"), shared)) char g_processName[0x104] = {0};
__attribute__((section("Shared"), shared)) char g_asiList[0xa28]     = {0};

static HMODULE g_hSelf = NULL;

static void GetModuleBaseName(char* out, int size) {
    char path[MAX_PATH];
    out[0] = 0;
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) return;
    char* p = path;
    for (char* s = path; *s; s++) if (*s == '\\' || *s == '/') p = s + 1;
    lstrcpynA(out, p, size);
}

static BOOL IsTargetProcess(void) {
    if (g_processName[0] == 0) return FALSE;
    char name[MAX_PATH];
    GetModuleBaseName(name, MAX_PATH);
    return _STRICMP(name, g_processName) == 0;
}

static DWORD WINAPI LoadPluginsThread(LPVOID lp) {
    if (g_injectDelay > 0) Sleep(g_injectDelay);

    char local[0xa28];
    lstrcpynA(local, g_asiList, sizeof(local));

    char* ctx = NULL;
    char* tok = strtok_s(local, "|", &ctx);
    while (tok) {
        while (*tok == ' ' || *tok == '\t') tok++;
        char* end = tok + lstrlenA(tok);
        while (end > tok) { end--; if (*end > ' ') break; *end = 0; }
        if (*tok) LoadLibraryA(tok);
        tok = strtok_s(NULL, "|", &ctx);
    }

    InterlockedExchange(&g_injectDone, 1);
    return 0;
}

extern "C" __declspec(dllexport) void SetASIPath(const char* path) { if (path) lstrcpynA(g_asiList, path, sizeof(g_asiList)); }
extern "C" __declspec(dllexport) void SetProcessName(const char* name) { if (name) lstrcpynA(g_processName, name, sizeof(g_processName)); }
extern "C" __declspec(dllexport) void SetInjectDelay(DWORD ms) { g_injectDelay = ms; }
extern "C" __declspec(dllexport) BOOL IsInjectionDone(void) { return g_injectDone != 0; }
extern "C" __declspec(dllexport) BOOL NeedInject(void) { return g_needInject != 0; }

extern "C" __declspec(dllexport) LRESULT HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (IsTargetProcess() && g_asiList[0] != 0) {
            if (InterlockedCompareExchange(&g_injectStarted, 1, 0) == 0) {
                CreateThread(NULL, 0, LoadPluginsThread, NULL, 0, NULL);
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID lp) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hSelf = hInst;
        DisableThreadLibraryCalls(hInst);
        if (IsTargetProcess()) {
            InterlockedExchange(&g_needInject, 1);
        }
    }
    return TRUE;
}