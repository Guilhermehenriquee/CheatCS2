@echo off
rem ============================================================================
rem  muei 無影 - MSVC build
rem
rem  Produces muei.exe next to this script with app.manifest embedded, so the
rem  process starts elevated. CS2 runs at High integrity; without that manifest
rem  every PROCESS_VM_READ is denied and the log fills with "Acesso negado".
rem
rem  Flags worth explaining:
rem    /std:c++20           std::filesystem plus inline variables are used
rem    /utf-8               sources contain 無影; MSVC misreads them otherwise
rem    /SUBSYSTEM:WINDOWS   entry point is wWinMain, not main
rem    /MANIFEST:NO         we embed app.manifest ourselves with mt.exe
rem    dxguid.lib           IID_IDXGIDevice / IID_ID3D11Texture2D live there
rem    d3dcompiler+dwmapi   pulled in by #pragma comment inside the ImGui
rem                         backends, no need to list them here
rem ============================================================================
setlocal enabledelayedexpansion

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if exist "!VCVARS!" (
    call "!VCVARS!" >nul 2>&1
) else (
    where cl >nul 2>&1
    if errorlevel 1 (
        echo [FAIL] cl.exe not found. Install "Desktop development with C++".
        exit /b 1
    )
)

cd /d "%~dp0"

if exist obj rmdir /s /q obj
mkdir obj

set "INCLUDES=/I . /I imgui /I imgui\backends /I vendor"
set "CFLAGS=/std:c++20 /EHsc /O2 /DNDEBUG /W4 /WX- /utf-8 /nologo /c"
set "CFLAGS=!CFLAGS! /D_CRT_SECURE_NO_WARNINGS /permissive- /Zc:__cplusplus"

echo [1/3] compiling

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
    cl !CFLAGS! !INCLUDES! "%%f" /Fo:obj\ >nul
    if errorlevel 1 (
        echo [FAIL] compile %%f
        cl !CFLAGS! !INCLUDES! "%%f" /Fo:obj\
        exit /b 1
    )
    echo       %%f
)

echo [2/3] linking

set "OBJS="
for %%f in (obj\*.obj) do set "OBJS=!OBJS! obj\%%~nxf"

link /nologo /OUT:muei.exe /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF ^
     !OBJS! user32.lib gdi32.lib d3d11.lib dxgi.lib dxguid.lib psapi.lib shlwapi.lib ole32.lib
if errorlevel 1 (
    echo [FAIL] link
    exit /b 1
)

echo [3/3] embedding manifest

mt -nologo -manifest app.manifest -outputresource:"muei.exe;#1"
if errorlevel 1 (
    echo [FAIL] mt.exe
    exit /b 1
)

echo.
echo BUILD_OK  muei.exe
for %%f in (muei.exe) do echo size        %%~zf bytes
echo.
echo First run writes muei.cfg and muei_log.txt beside the exe.
echo Optional: drop offsets.cfg beside the exe to override any offset by name.

endlocal
exit /b 0
