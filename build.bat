@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\Csmilly\Desktop\cs2ext

rem Compile separate objects first so a failed link cannot leave a stale exe.
cl /std:c++17 /EHsc /O2 /DNDEBUG /W3 /WX- /utf-8 /nologo /c main.cpp /I imgui /I imgui\backends
if errorlevel 1 ( echo COMPILE_FAILED & exit /b 1 )

rem Link with NO auto manifest snippet, then embed ours with mt.exe.
link /nologo /OUT:main.exe /MANIFEST:NO main.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_win32.obj imgui_impl_dx11.obj user32.lib gdi32.lib d3d11.lib dxgi.lib psapi.lib
if errorlevel 1 ( echo LINK_FAILED & exit /b 1 )

mt -nologo -manifest app.manifest -outputresource:main.exe;#1
if errorlevel 1 ( echo MANIFEST_FAILED & exit /b 1 )

echo BUILD_OK
