@echo off
setlocal
cd /d "%~dp0"

echo [1/2] Building SecureMemo.exe ...
call compile.bat
if errorlevel 1 exit /b 1

set "ISCC=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo Inno Setup 6 not found. Install JRSoftware.InnoSetup via winget.
  exit /b 1
)

if not exist dist mkdir dist
echo [2/2] Compiling installer ...
"%ISCC%" "installer\SecureMemo.iss"
if errorlevel 1 exit /b 1
echo.
echo Done: dist\SecureMemo_Setup.exe
dir dist\SecureMemo_Setup.exe
