@echo off
setlocal
set "MINGW=C:\Users\soodalpie\AppData\Local\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-ucrt-x86_64\bin"
set "PATH=%MINGW%;C:\Program Files\CMake\bin;%PATH%"
cd /d "%~dp0"
if exist build rmdir /s /q build
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
if errorlevel 1 exit /b 1
cmake --build build -j 4
exit /b %ERRORLEVEL%
