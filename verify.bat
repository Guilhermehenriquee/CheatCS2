@echo off
rem ============================================================================
rem  muei 無影 - build and run the live offset verifier
rem
rem  Console program. Run it from an elevated shell while CS2 is in a map.
rem  Exit code 0 means every checked offset resolved.
rem ============================================================================
setlocal enabledelayedexpansion

cd /d "%~dp0"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS!" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if exist "!VCVARS!" (
    call "!VCVARS!" >nul 2>&1
    where cl >nul 2>&1
)

where cl >nul 2>&1
if errorlevel 1 goto mingw

echo [MSVC] building verify.exe
cl /std:c++20 /EHsc /O2 /DNDEBUG /W4 /WX- /utf-8 /nologo /D_CRT_SECURE_NO_WARNINGS ^
   /I . /I imgui /I imgui\backends /I vendor ^
   verify.cpp core\Log.cpp core\ProcUser.cpp sdk\Offsets.cpp sdk\Nav.cpp ^
   /Fe:verify.exe /link /SUBSYSTEM:CONSOLE user32.lib psapi.lib shlwapi.lib
if errorlevel 1 (
    echo [FAIL] build
    exit /b 1
)
goto run

:mingw
if "%MINGW%"=="" set "MINGW=C:\mingw64"
set "PATH=!MINGW!\bin;%PATH%"
where g++ >nul 2>&1
if errorlevel 1 (
    echo [FAIL] no cl.exe and no g++. Nothing to build with.
    exit /b 1
)

echo [MinGW] building verify.exe
g++ -std=c++20 -O2 -DNDEBUG -Wall -Wno-unknown-pragmas -static -static-libgcc -static-libstdc++ ^
    -I. -Iimgui -Iimgui\backends -Ivendor ^
    verify.cpp core\Log.cpp core\ProcUser.cpp sdk\Offsets.cpp sdk\Nav.cpp ^
    -o verify.exe -luser32 -lpsapi -lshlwapi
if errorlevel 1 (
    echo [FAIL] build
    exit /b 1
)

:run
echo.
verify.exe
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo VERIFY_OK) else (echo VERIFY_FAILED)
endlocal & exit /b %RC%
