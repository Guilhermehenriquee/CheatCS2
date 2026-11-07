@echo off
rem ===========================================================================
rem  muei 無影 - run helper
rem
rem  Right-click this file and pick "Run as administrator", or launch it from an
rem  already elevated terminal. CS2 runs at High integrity; a non-elevated muei
rem  gets ERROR_ACCESS_DENIED on every PROCESS_VM_READ and attaches to nothing.
rem
rem  What it does, in order:
rem    1. refuses to continue if not elevated
rem    2. builds muei.exe if it is missing or older than main.cpp
rem    3. builds the verifier and runs it, so you see offset PASS/FAIL first
rem    4. starts muei.exe
rem ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ==========================================================
echo  muei  -  run helper
echo ==========================================================
echo.

rem ---------------------------------------------------------------- elevate --
net session >nul 2>&1
if errorlevel 1 (
    echo [FAIL] not elevated.
    echo.
    echo   Right-click run.bat  ^>  "Run as administrator"
    echo.
    echo   Without it: OpenProcess^(VM_READ^) returns error 5 and muei
    echo   attaches to nothing, which looks like "the menu works but shows
    echo   no players".
    echo.
    pause
    exit /b 1
)
echo [ok] running elevated
echo.

rem ------------------------------------------------------------------ build --
set "NEED_BUILD=0"
if not exist muei.exe set "NEED_BUILD=1"
for %%f in (muei.exe) do set "EXE_TIME=%%~tf"
for %%f in (main.cpp) do set "SRC_TIME=%%~tf"
if "!EXE_TIME!" LSS "!SRC_TIME!" set "NEED_BUILD=1"

if "!NEED_BUILD!"=="1" (
    echo [..] muei.exe missing or older than sources, building
    call build.bat
    if errorlevel 1 (
        echo [FAIL] build failed
        pause
        exit /b 1
    )
)
echo [ok] muei.exe present
echo.

rem --------------------------------------------------------------- verifier --
echo [..] building the offset verifier
set "HAVE_CL=0"
where cl >nul 2>&1 && set "HAVE_CL=1"
if "!HAVE_CL!"=="0" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if not exist "!VCVARS!" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if exist "!VCVARS!" call "!VCVARS!" >nul 2>&1
)

if exist "!VCVARS!" (
    cl /std:c++20 /EHsc /O2 /DNDEBUG /W4 /WX- /utf-8 /nologo /D_CRT_SECURE_NO_WARNINGS ^
       /I . /I imgui /I imgui\backends /I vendor ^
       verify.cpp core\Log.cpp core\ProcUser.cpp sdk\Offsets.cpp sdk\Nav.cpp ^
       /Fe:verify.exe /link /SUBSYSTEM:CONSOLE user32.lib psapi.lib shlwapi.lib >nul
) else (
    set "MINGW=C:\mingw64"
    if exist "!MINGW!\bin\g++.exe" (
        "!MINGW!\bin\g++.exe" -std=c++20 -O2 -DNDEBUG -Wall -Wno-unknown-pragmas ^
            -static -static-libgcc -static-libstdc++ ^
            -I. -Iimgui -Iimgui\backends -Ivendor ^
            verify.cpp core\Log.cpp core\ProcUser.cpp sdk\Offsets.cpp sdk\Nav.cpp ^
            -o verify.exe -luser32 -lpsapi -lshlwapi >nul
    )
)

tasklist /fi "imagename eq cs2.exe" 2>nul | find /i "cs2.exe" >nul
if errorlevel 1 (
    echo [!!] cs2.exe is not running.
    echo      Start CS2, load a map, then run this helper again.
    echo      Offsets can only be proven against a live process.
    echo.
    pause
    exit /b 1
)
echo [ok] cs2.exe is running
echo.

if not exist verify.exe (
    echo [!!] verifier was not built, skipping the offset check
    echo.
    goto launch
)

echo ==========================================================
echo  offset check
echo ==========================================================
echo.
verify.exe
echo.
echo ==========================================================
echo.

:launch
echo [..] starting muei.exe
start "" muei.exe
echo.
echo  Insert   open / close the menu
echo  End      quit
echo.
echo  Log: %~dp0muei_log.txt
echo  Config: %~dp0muei.cfg
echo.
echo  If the menu opens but shows "0 players":
echo    - CS2 must be in a match, not the main menu
echo    - muei must be elevated (this script checks that)
echo    - read the log; the attach section names the exact failure
echo.
endlocal
exit /b 0
