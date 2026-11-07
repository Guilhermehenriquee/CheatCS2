@echo off
rem ============================================================================
rem  muei 無影 - MinGW-w64 build
rem
rem  Same sources as build.bat, different toolchain. This path exists because it
rem  is the one that was used to verify the tree end to end without a Visual
rem  Studio install: every translation unit compiles and the whole program links.
rem
rem  Set MINGW to your toolchain root if it is not C:\mingw64.
rem ============================================================================
setlocal enabledelayedexpansion

if "%MINGW%"=="" set "MINGW=C:\mingw64"
set "PATH=!MINGW!\bin;%PATH%"

cd /d "%~dp0"

where g++ >nul 2>&1
if errorlevel 1 (
    echo [FAIL] g++ not on PATH. Set MINGW=C:\path\to\mingw64
    exit /b 1
)

if exist obj-mingw rmdir /s /q obj-mingw
mkdir obj-mingw

set "INC=-I. -Iimgui -Iimgui\backends -Ivendor"
set "CXXFLAGS=-std=c++20 -O2 -DNDEBUG -Wall -Wno-unknown-pragmas !INC!"

echo [1/2] compiling

for %%f in (
    main.cpp
    core\Log.cpp core\ProcUser.cpp core\ProcKernel.cpp core\ProcDMA.cpp core\Config.cpp
    sdk\Offsets.cpp sdk\Nav.cpp
    features\Input.cpp features\Resolver.cpp features\Aimbot.cpp features\Triggerbot.cpp
    features\Bhop.cpp features\Glow.cpp features\RCS.cpp features\SkinChanger.cpp
    features\Misc.cpp features\ESP.cpp
    ui\Theme.cpp ui\Menu.cpp ui\Overlay.cpp
    imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp
    imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp
) do (
    set "OBJ=obj-mingw\%%~nf.o"
    g++ !CXXFLAGS! -c "%%f" -o "!OBJ!"
    if errorlevel 1 (
        echo [FAIL] compile %%f
        exit /b 1
    )
    echo       %%f
)

echo [2/2] linking

set "OBJS="
for %%f in (obj-mingw\*.o) do set "OBJS=!OBJS! obj-mingw\%%~nxf"

g++ -mwindows -municode -static -static-libgcc -static-libstdc++ -o muei.exe !OBJS! ^
    -ld3d11 -ldxgi -ldxguid -ld3dcompiler -ldwmapi ^
    -luser32 -lgdi32 -lpsapi -lshlwapi -lole32 -luuid
if errorlevel 1 (
    echo [FAIL] link
    exit /b 1
)

echo.
echo BUILD_OK  muei.exe (MinGW)
for %%f in (muei.exe) do echo size        %%~zf bytes
echo.
echo Note: this build has no embedded manifest. For the elevated behaviour run
echo it from an already elevated shell, or use build.bat with MSVC.

endlocal
exit /b 0
