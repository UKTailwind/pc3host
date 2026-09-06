@echo off
rem Put the Pico Computer 3's tools on this account's PATH, and teach
rem Windows what a .bc file is, so a program you build runs by its name.
rem No administrator rights: this account's environment, and
rem HKEY_CURRENT_USER for the file type.
rem
rem   install.cmd            do it
rem   install.cmd /q         do it without waiting for a key
rem
rem uninstall.cmd beside this file takes it all out again.
setlocal
set "PC3=%~dp0"
if "%PC3:~-1%"=="\" set "PC3=%PC3:~0,-1%"

if not exist "%PC3%\bin\cc.exe" (
    echo This does not look like the pc3host folder - %PC3%\bin\cc.exe is missing.
    goto :done
)

echo Installing for %USERNAME% from %PC3%
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%PC3%\pc3env.ps1" -Add "%PC3%"
if errorlevel 1 (
    echo.
    echo Something went wrong; nothing may have changed.  Run uninstall.cmd
    echo to be sure, then try again.
    goto :done
)
echo.
echo Done.  OPEN A NEW COMMAND PROMPT - this one still has the old
echo environment - and then, from any folder:
echo.
echo     cc -r myprog.bas         build it and run it
echo     cc myprog.bas            build it: myprog.bc
echo     .\myprog.bc              run what you built
echo     bcrun myprog.bc          anywhere, and in PowerShell
echo     mmbedit myprog.bas        the editor; F2 builds and runs
echo.
echo Note the BACKslash in .\myprog.bc - a command prompt does not
echo take ./ for a program in the current folder, though PowerShell
echo does.  A .bc file is now a program Windows knows how to open, so
echo double clicking one runs it too.

:done
if /i not "%~1"=="/q" pause
endlocal
