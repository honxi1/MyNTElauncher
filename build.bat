@echo off
rem ============================================
rem  build.bat - build MOD launcher
rem  outputs: MyLauncher.exe + MyInject.dll
rem  requires: MinGW-w64 (g++, windres) in PATH
rem ============================================
setlocal
cd /d "%~dp0"

rem auto-detect MinGW bin dir (no hardcoded path)
where g++ >nul 2>nul
if errorlevel 1 (
    for /f "delims=" %%i in ('where g++ 2^>nul ^| findstr /i mingw') do set "MINGW=%%~dpi"
    if defined MINGW set "PATH=%MINGW%;%PATH%"
)
where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ not found in PATH. Install MinGW-w64 and add its bin dir to PATH.
    exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 (
    echo [ERROR] windres not found in PATH.
    exit /b 1
)

echo [1/4] compiling MyInject.dll ...
g++ -c -O2 -std=c++17 -o MyInject.o MyInject.cpp
if errorlevel 1 goto :err
g++ -shared -O2 -o MyInject.dll MyInject.o -static
if errorlevel 1 goto :err

echo [2/4] building resources (embed MyInject.dll) ...
windres -O coff -i launcher.rc -o launcher_res.o
if errorlevel 1 goto :err

echo [3/4] compiling launcher.exe ...
g++ -c -O2 -std=c++17 -mwindows -finput-charset=UTF-8 -fexec-charset=GBK -o launcher.o launcher.cpp
if errorlevel 1 goto :err

echo [4/4] linking ...
g++ -mwindows -o MyLauncher.exe launcher.o launcher_res.o -luser32 -lgdi32 -lcomdlg32 -lshell32 -ladvapi32 -static
if errorlevel 1 goto :err

echo.
echo BUILD OK:
echo   MyInject.dll   (inject carrier)
echo   MyLauncher.exe (launcher, run as admin)
del MyInject.o launcher.o launcher_res.o 2>nul
goto :eof

:err
echo.
echo BUILD FAILED!
exit /b 1
