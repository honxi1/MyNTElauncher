// MyInject.cpp - 注入载体DLL
// 原理：由启动器 SetWindowsHookExW 注入到游戏进程，
// HookProc 在游戏进程消息循环启动后触发，此时游戏主模块已加载，
// 由 HookProc 创建线程加载 ASI/插件，避免时序问题。
#include <windows.h>
#include <string.h>
#include <stdio.h>

// 记录 ASI 加载结果到 %TEMP%\MyInject_load.log（排查"注入完成但mod无效"）
static void LogLoad(const char* path, HMODULE m) {
    char tmp[MAX_PATH], p[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmp)) return;
    wsprintfA(p, "%sMyInject_load.log", tmp);
    FILE* f = fopen(p, "a");
    if (f) {
        fprintf(f, "[%lu] %s -> 0x%p err=%lu\n",
                GetTickCount(), path, m, m ? 0 : GetLastError());
        fclose(f);
    }
}

// ============ Shared 段（跨进程共享状态）============
//   0x6000  g_injectDone   DWORD  注入完成标志
//   0x6004  g_injectDelay  DWORD  注入延迟(ms)
//   0x6008  g_needInject   DWORD  HookProc需要注入标志
//   0x6010  g_processName  char[0x104]  目标进程名
//   0x6120  g_asiList      char[0xa28]  插件列表（|分隔）
#pragma section("Shared", read, write, shared)
__attribute__((section("Shared"), shared)) volatile LONG   g_injectDone   = 0;
__attribute__((section("Shared"), shared)) volatile DWORD  g_injectDelay  = 0;
__attribute__((section("Shared"), shared)) volatile LONG   g_needInject   = 0;
__attribute__((section("Shared"), shared)) char g_processName[0x104] = {0};
__attribute__((section("Shared"), shared)) char g_asiList[0xa28]     = {0};

// 当前进程模块名（不含路径）
static void GetModuleBaseName(char* out, int size) {
    char path[MAX_PATH];
    out[0] = 0;
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) return;
    char* p = strrchr(path, '\\');
    if (!p) p = strrchr(path, '/');
    const char* name = p ? p + 1 : path;
    lstrcpynA(out, name, size);
}

// 判断当前进程是否是目标游戏进程（进程名匹配，不区分大小写）
static bool IsTargetProcess() {
    if (g_processName[0] == 0) return false;
    char name[MAX_PATH];
    GetModuleBaseName(name, MAX_PATH);
    return _stricmp(name, g_processName) == 0;
}

// 加载线程：延迟等待后逐个加载插件
static DWORD WINAPI LoadPluginsThread(LPVOID) {
    if (g_injectDelay > 0) {
        Sleep(g_injectDelay);
    }

    // 复制到本地缓冲，用strtok按'|'分隔逐个LoadLibraryA
    char local[0xa28];
    lstrcpynA(local, g_asiList, sizeof(local));

    char* ctx = NULL;
    char* tok = strtok_s(local, "|", &ctx);
    while (tok) {
        // 去除首尾空格
        while (*tok == ' ' || *tok == '\t') tok++;
        char* end = tok + strlen(tok) - 1;
        while (end >= tok && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = 0;
        if (*tok) {
            HMODULE m = LoadLibraryA(tok);
            LogLoad(tok, m);
        }
        tok = strtok_s(NULL, "|", &ctx);
    }

    InterlockedExchange(&g_injectDone, 1);   // 标记注入完成
    return 0;
}

// ============ 导出函数 ============

// 设置插件路径列表（'|'分隔多个）
extern "C" __declspec(dllexport) void SetASIPath(const char* path) {
    if (path) lstrcpynA(g_asiList, path, sizeof(g_asiList));
}

// 设置目标进程名
extern "C" __declspec(dllexport) void SetProcessName(const char* name) {
    if (name) lstrcpynA(g_processName, name, sizeof(g_processName));
}

// 设置注入延迟
extern "C" __declspec(dllexport) void SetInjectDelay(DWORD ms) {
    g_injectDelay = ms;
}

// 查询是否已注入完成
extern "C" __declspec(dllexport) BOOL IsInjectionDone(void) {
    return g_injectDone != 0;
}

// 查询是否需要注入（HookProc内部使用）
extern "C" __declspec(dllexport) BOOL NeedInject(void) {
    return g_needInject != 0;
}

// HookProc：WH_CALLWNDPROC 回调，在游戏进程处理窗口消息时触发
extern "C" __declspec(dllexport) LRESULT HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        // 目标进程 && 有插件列表 && 尚未注入 → 创建线程加载
        if (IsTargetProcess() && g_asiList[0] != 0 && g_injectDone == 0) {
            if (InterlockedCompareExchange(&g_injectDone, 1, 0) == 0) {
                CreateThread(NULL, 0, LoadPluginsThread, NULL, 0, NULL);
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ============ DllMain ============
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        // 若当前进程就是目标游戏进程，标记需要注入
        if (IsTargetProcess()) {
            InterlockedExchange(&g_needInject, 1);
        }
    }
    return TRUE;
}
