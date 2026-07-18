@echo off
setlocal EnableExtensions
set "MINGW=C:\Users\soodalpie\AppData\Local\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-ucrt-x86_64\bin"
set "PATH=%MINGW%;%PATH%"
cd /d "%~dp0"
if not exist out mkdir out

set "CXX=clang++"
REM Do NOT use -fexec-charset=UTF-8: breaks L"" wchar (UTF-16) on Windows MinGW
set "CXXFLAGS=-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -DUNICODE -D_UNICODE -municode -finput-charset=UTF-8 -I src"
set "LIBS=-lbcrypt -lshell32 -lcomctl32 -lole32 -luser32 -lgdi32 -ladvapi32 -lrpcrt4 -luuid -ldwmapi -lcomdlg32 -luxtheme -lole32 -mwindows"
set "SRCS=src\main.cpp src\app\Application.cpp src\app\NoteManager.cpp src\app\SecurityGuard.cpp src\crypto\Auth.cpp src\crypto\Crypto.cpp src\crypto\VaultStorage.cpp src\crypto\PublicIndex.cpp src\model\Note.cpp src\ui\LockDialog.cpp src\ui\NoteWindow.cpp src\ui\NotesListWindow.cpp src\ui\TrayIcon.cpp"

windres -O coff resources\app.rc -o out\app.res
if errorlevel 1 goto nores
%CXX% %CXXFLAGS% %SRCS% out\app.res -o out\SecureMemo.exe %LIBS%
goto done
:nores
%CXX% %CXXFLAGS% %SRCS% -o out\SecureMemo.exe %LIBS%
:done
if errorlevel 1 exit /b 1
echo Built out\SecureMemo.exe
dir out\SecureMemo.exe
