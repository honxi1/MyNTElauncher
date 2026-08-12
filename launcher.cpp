// Launcher.cpp - MOD启动器
// 流程：提权SeDebugPrivilege → 从自身资源提取MyInject.dll到临时目录
//       → LoadLibraryA + GetProcAddress获取导出 → 配置注入参数
//       → SetWindowsHookExW(WH_CALLWNDPROC, HookProc) 全局钩子注入
//       → 用户手动启动游戏 → 轮询IsInjectionDone
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ============ 全局状态 ============
static HMODULE g_hMyInject = NULL;
static FARPROC g_fnSetASIPath    = NULL;
static FARPROC g_fnSetProcessName= NULL;
static FARPROC g_fnSetInjectDelay= NULL;
static FARPROC g_fnIsInjectionDone=NULL;
static FARPROC g_fnHookProc      = NULL;

static HWND g_hwndMain   = NULL;      // 主窗口
static HWND g_hwndLog    = NULL;      // 日志框
static bool g_bInjected  = false;     // 是否已注入完成
static bool g_bInjectionStarted = false; // 防止重复注入
static UINT_PTR g_timer  = 0;
static int g_countdown   = 5;         // 注入完成后的倒计时(秒)

// 配置（硬编码，无需 ini）
static std::string g_processName = "HTGame.exe"; // 目标游戏进程名
static std::string g_asiList;                    // ASI/DLL 列表（|分隔，绝对路径）
static DWORD g_injectDelay = 1000;               // 注入延迟(ms)

// ============ 工具函数 ============
static void AddLog(const char* text) {
    if (g_hwndLog) {
        // 追加到日志框并滚动到底部
        int len = GetWindowTextLengthA(g_hwndLog);
        SendMessageA(g_hwndLog, EM_SETSEL, len, len);
        SendMessageA(g_hwndLog, EM_REPLACESEL, FALSE, (LPARAM)text);
        SendMessageA(g_hwndLog, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
        SendMessageA(g_hwndLog, EM_SCROLLCARET, 0, 0);
    }
}

static void AddLogF(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    AddLog(buf);
}

// 提升到 SeDebugPrivilege（管理员运行时可用）
static void EnableDebugPrivilege() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return;
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    LookupPrivilegeValueA(NULL, "SeDebugPrivilege", &tp.Privileges[0].Luid);
    AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(hToken);
}

// 从自身资源提取内嵌DLL到临时目录，返回DLL路径（用全局s_dllPath保存）
static bool ExtractEmbeddedDll() {
    HRSRC hr = FindResourceA(NULL, MAKEINTRESOURCE(101), RT_RCDATA);
    if (!hr) return false;
    HGLOBAL hg = LoadResource(NULL, hr);
    if (!hg) return false;
    void* data = LockResource(hg);
    DWORD size = SizeofResource(NULL, hr);
    if (!data || size == 0) return false;

    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);

    // 构造临时文件名: %TEMP%\MyInject_<pid>.dll（注入载体，保持原样）
    static char s_dllPath[MAX_PATH];
    wsprintfA(s_dllPath, "%sMyInject_%lu.dll", tmpPath, GetCurrentProcessId());

    // 写文件
    HANDLE hf = CreateFileA(s_dllPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(hf, data, size, &written, NULL);
    CloseHandle(hf);
    if (!ok || written != size) {
        DeleteFileA(s_dllPath);
        return false;
    }
    return true;
}

// 获取当前exe所在目录（带结尾反斜杠）
static std::string GetExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* p = strrchr(path, '\\');
    if (p) *(p + 1) = 0;
    return path;
}

// 初始化配置（硬编码默认值，无需 ini）
static void LoadConfig() {
    // ASI 主插件：exe 同目录的 UniversalSigBypasser.asi（原版名字，绝对路径，因为注入线程在游戏进程内LoadLibraryA）
    g_asiList = GetExeDir() + "UniversalSigBypasser.asi";
}

// 注入主流程
static void DoInjection() {
    if (g_bInjectionStarted) return;
    g_bInjectionStarted = true;
    AddLog("[启动] 注入流程开始");

    EnableDebugPrivilege();
    AddLog("[提权] SeDebugPrivilege 已尝试");

    // 1. 初始化配置（硬编码，无需 ini）
    LoadConfig();
    AddLogF("[配置] 进程名=%s 延迟=%lu", g_processName.c_str(), g_injectDelay);
    AddLogF("[配置] 插件列表=%s", g_asiList.c_str());

    // 2. 提取内嵌 MyInject.dll
    if (!ExtractEmbeddedDll()) {
        AddLogF("[错误] 提取MyInject.dll失败 错误=%lu", GetLastError());
        return;
    }
    AddLog("[提取] MyInject.dll 已释放到临时目录");

    // 3. 加载载体DLL
    static char s_dllPath[MAX_PATH];
    {
        char tmpPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        wsprintfA(s_dllPath, "%sMyInject_%lu.dll", tmpPath, GetCurrentProcessId());
    }

    g_hMyInject = LoadLibraryA(s_dllPath);
    if (!g_hMyInject) {
        AddLogF("[错误] LoadLibraryA失败 错误=%lu", GetLastError());
        return;
    }
    AddLogF("[加载] MyInject.dll 已加载 hMod=0x%p", g_hMyInject);

    // 4. 获取导出函数
    g_fnSetASIPath     = GetProcAddress(g_hMyInject, "SetASIPath");
    g_fnSetProcessName = GetProcAddress(g_hMyInject, "SetProcessName");
    g_fnSetInjectDelay = GetProcAddress(g_hMyInject, "SetInjectDelay");
    g_fnIsInjectionDone= GetProcAddress(g_hMyInject, "IsInjectionDone");
    g_fnHookProc       = GetProcAddress(g_hMyInject, "HookProc");
    if (!g_fnSetASIPath || !g_fnSetProcessName || !g_fnSetInjectDelay ||
        !g_fnIsInjectionDone || !g_fnHookProc) {
        AddLog("[错误] 获取导出函数失败");
        return;
    }
    AddLog("[导出] 5个函数均已获取");

    // 5. 配置注入参数
    ((void(*)(const char*))g_fnSetASIPath)(g_asiList.c_str());
    ((void(*)(const char*))g_fnSetProcessName)(g_processName.c_str());
    ((void(*)(DWORD))g_fnSetInjectDelay)(g_injectDelay);
    AddLog("[配置] 注入参数已下发到 MyInject");

    // 6. 安装全局钩子（WH_CALLWNDPROC，注入所有进程）
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, (HOOKPROC)g_fnHookProc, g_hMyInject, 0);
    if (!hook) {
        AddLogF("[错误] SetWindowsHookExW失败 错误=%lu", GetLastError());
        return;
    }
    AddLog("[注入] 全局Hook已安装 (WH_CALLWNDPROC)");
    AddLog("[提示] 请自行启动游戏，游戏创建窗口后会自动注入");

    // 7. 轮询注入状态（timer 挂在主窗口上）
    g_bInjected = false;
    g_timer = SetTimer(g_hwndMain, 1, 500, NULL);
    AddLog("[等待] 等待游戏加载插件(IsInjectionDone)...");
}

// ============ 窗口 ============
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
    {
        // 多行日志框（只读）
        g_hwndLog = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
            WS_VSCROLL | ES_LEFT,
            10, 10, 380, 100, hwnd, NULL, GetModuleHandleA(NULL), NULL);
        return 0;
    }
    case WM_TIMER:
        if (g_bInjected) {
            // 注入完成，倒计时自动关闭
            if (g_countdown <= 1) {
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);   // 触发 WM_DESTROY 退出
            } else {
                g_countdown--;
                AddLogF("[倒计时] %d 秒后自动关闭...", g_countdown);
            }
            return 0;
        }
        if (g_fnIsInjectionDone) {
            if (((BOOL(*)())g_fnIsInjectionDone)()) {
                KillTimer(hwnd, 1);
                AddLog("[完成] 注入完成，5秒后自动关闭");
                g_bInjected = true;
                g_countdown = 5;
                g_timer = SetTimer(hwnd, 1, 1000, NULL);   // 改为1秒倒计时
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    // 窗口类
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = L"MyLauncherWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"MyLauncherWnd", L"异环 MOD 启动器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 160, NULL, NULL, hInst, NULL);
    if (!hwnd) return 0;
    g_hwndMain = hwnd;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 窗口显示后自动开始注入
    DoInjection();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
