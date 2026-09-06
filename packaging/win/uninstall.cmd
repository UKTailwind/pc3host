@echo off
rem Undo install.cmd: take this folder off the account's PATH, forget
rem what a .bc file is, and drop .BC from PATHEXT.  The folder itself is
rem left alone - delete it when you want the tools gone.
rem
rem   uninstall.cmd          do it
rem   uninstall.cmd /q       do it without waiting for a key
setlocal
set "PC3=%~dp0"
if "%PC3:~-1%"=="\" set "PC3=%PC3:~0,-1%"

echo Removing the installation for %USERNAME%
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%PC3%\pc3env.ps1" -Remove "%PC3%"
echo.
echo Done.  Open a new command prompt for it to take effect.

if /i not "%~1"=="/q" pause
endlocal
