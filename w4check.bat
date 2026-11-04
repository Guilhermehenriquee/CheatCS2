@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\Csmilly\Desktop\cs2ext
cl /std:c++17 /EHsc /O2 /DNDEBUG /W4 /nologo /c main.cpp /I imgui /I imgui\backends /Fo:nul 2>&1
