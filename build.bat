@echo off
REM ============================================================
REM  Lexara (FontExact) — build script (MSVC 32-bit, CMake)
REM ============================================================
REM  Requirements:
REM    - Visual Studio 2019 or 2022 (MSVC v142 / v143)
REM    - CMake 3.15+ installed
REM    - third_party\ folder present (Detours, freetype, msdfgen,
REM      unordered_dense)
REM  Usage: double-click this file. No need to open Developer
REM         Command Prompt manually.
REM ============================================================

setlocal

REM Auto-locate vcvarsall.bat for VS 2022 or 2019
set VCVARS=""
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
)

if %VCVARS%=="" (
    echo [ERROR] Visual Studio not found. Install VS 2019 or 2022.
    pause
    exit /b 1
)

REM Initialize 32-bit MSVC environment
call %VCVARS% x86

REM Change to the directory where this .bat lives
cd /d "%~dp0"

set BUILD_DIR=%~dp0build

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cd /d "%BUILD_DIR%"

echo.
echo [Lexara] Configuring with CMake...
echo.

cmake .. -A Win32 -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

echo.
echo [Lexara] Building...
echo.

cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo [OK] Build complete.
echo      Output: %BUILD_DIR%\Release\dinput8.dll
echo      Copy dinput8.dll to your WoW 3.3.5 root folder.
echo.
pause