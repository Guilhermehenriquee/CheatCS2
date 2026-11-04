@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\Csmilly\Desktop\cs2ext
cl /std:c++17 /EHsc /O2 /DNDEBUG /W3 /nologo /utf-8 /c verify.cpp
if errorlevel 1 ( echo COMPILE_FAILED & exit /b 1 )
link /nologo /OUT:verify.exe /MANIFEST:NO verify.obj user32.lib psapi.lib
if errorlevel 1 ( echo LINK_FAILED & exit /b 1 )
mt -nologo -manifest app.manifest -outputresource:verify.exe;#1
if errorlevel 1 ( echo MANIFEST_FAILED & exit /b 1 )
echo VERIFY_OK
