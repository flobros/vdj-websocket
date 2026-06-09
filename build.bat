@echo off
setlocal

cd /d "%~dp0"

:: ── Architecture (x64 default, pass x86 for 32-bit) ──────────────────────────
set ARCH=x64
if /i "%1"=="x86" set ARCH=x86
if /i "%1"=="32"  set ARCH=x86

:: ── Locate VirtualDJ Plugins folder ──────────────────────────────────────────
if "%ARCH%"=="x86" (
    set OUTDIR=%LOCALAPPDATA%\VirtualDJ\Plugins\Generics
) else (
    set OUTDIR=%LOCALAPPDATA%\VirtualDJ\Plugins64\Generics
)
if not exist "%LOCALAPPDATA%\VirtualDJ" (
    if "%ARCH%"=="x86" (
        set OUTDIR=%USERPROFILE%\Documents\VirtualDJ\Plugins\Generics
    ) else (
        set OUTDIR=%USERPROFILE%\Documents\VirtualDJ\Plugins64\Generics
    )
)

:: ── Locate MSVC via vswhere ───────────────────────────────────────────────────
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo ERROR: vswhere.exe not found.
    echo Install Visual Studio 2019 or later with the "Desktop development with C++" workload.
    pause & exit /b 1
)

for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS_PATH=%%i
if "%VS_PATH%"=="" (
    echo ERROR: No Visual Studio installation with C++ tools found.
    echo Install the "Desktop development with C++" workload in Visual Studio Installer.
    pause & exit /b 1
)

if "%ARCH%"=="x86" (
    set VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars32.bat
) else (
    set VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat
)

call "%VCVARS%"
if errorlevel 1 (
    echo ERROR: Could not initialize MSVC environment from:
    echo   %VCVARS%
    pause & exit /b 1
)

:: ── Build ─────────────────────────────────────────────────────────────────────
echo Building %ARCH%...
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /nologo /O2 /W3 /EHsc /LD /I sdk NowPlaying.cpp /Fe:"%OUTDIR%\NowPlaying.dll" /link /DLL
if errorlevel 1 (
    echo.
    echo BUILD FAILED
    pause & exit /b 1
)

:: ── Install INI ───────────────────────────────────────────────────────────────
if exist "NowPlaying.ini" (
    copy /Y "NowPlaying.ini" "%OUTDIR%\NowPlaying.ini" >nul
    echo Installed: %OUTDIR%\NowPlaying.ini
) else (
    echo NOTE: NowPlaying.ini not found - plugin will use built-in defaults ^(no auth^)
)

echo.
echo BUILD SUCCESS [%ARCH%]
echo Installed: %OUTDIR%\NowPlaying.dll
echo Restart VirtualDJ to load the plugin.
pause
