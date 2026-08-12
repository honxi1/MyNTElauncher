# 异环 MOD 启动器

《异环》签名绕过补丁加载器。通过全局钩子把签名绕过补丁注入游戏进程，支持加载 mod 的 pak/utoc/ucas 文件。

## 使用

1. 从 [Releases](../../releases) 下载，或自行编译（见下）
2. 将 `UniversalSigBypasser.asi` 放在启动器同目录
3. **右键管理员运行** `MyLauncher（右键管理启动）.exe`
4. 启动器自动安装钩子，窗口提示后手动启动游戏
5. 游戏创建窗口后自动注入补丁，约 5 秒后启动器自动关闭

## MOD 安装路径

把 `.pak` 放到游戏目录的 `~mods` 文件夹：

```
<游戏目录>\Client\WindowsNoEditor\HT\Content\Paks\~mods
```

- 游戏默认目录名是 `Neverness To Everness`
- `~mods` 文件夹没有就自己建（注意带 `~` 波浪号）
- 示例：`D:\Neverness To Everness\Client\WindowsNoEditor\HT\Content\Paks\~mods\你的mod.pak`

## 工作原理

```
MyLauncher（右键管理启动）.exe（管理员）
  ├─ 从自身资源释放注入载体 MyInject.dll 到 %TEMP%
  ├─ SetWindowsHookExW(WH_CALLWNDPROC, HookProc) 全局钩子
  └─ 游戏进程创建窗口时，HookProc 触发
        └─ 在游戏进程内 LoadLibraryA(UniversalSigBypasser.asi) 完成注入
```

## 编译

依赖：MinGW-w64（g++、windres，需在 PATH 中）

```
build.bat
```

产物：`MyLauncher.exe`、`MyInject.dll`

## 目录结构

```
├── launcher.cpp       # 启动器（提权/释放载体/装钩子/轮询）
├── MyInject.cpp       # 注入载体 DLL（Shared段 + HookProc）
├── launcher.rc        # 资源脚本（内嵌 MyInject.dll + 管理员 manifest）
├── launcher.manifest  # asInvoker（配合右键管理员运行，避免被反作弊标记）
└── build.bat          # 一键编译
```

## 声明

- `UniversalSigBypasser.asi` 补丁来自 [rm-NoobInCoding/UniversalSigBypasser](https://github.com/rm-NoobInCoding/UniversalSigBypasser)，作者 rm-NoobInCoding，许可证 [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/)（署名-非商业性使用）
- 本启动器源码仅用于学习交流，请遵守游戏用户协议
