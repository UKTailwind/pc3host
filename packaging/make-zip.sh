#!/bin/bash
#
# Build the Windows package: dist/pc3host-<version>-win64.zip
#
#   bash packaging/make-zip.sh                # from the submodule's FUZIX
#   FUZIX_ROOT=/home/me/src/FUZIX bash packaging/make-zip.sh
#
# Cross-built from Linux with mingw-w64 (cmake/toolchain-mingw64.cmake),
# which is how the Debian package is built too - one tree, two outputs.
# Everything is linked statically, so the zip is unpacked anywhere and
# the programs run: no runtime to install, nothing on the PATH.
#
# The layout is the board's, as the .deb has it under /opt/pc3:
#
#   pc3host\bin\           the programs, cc among them, and pc3d
#   pc3host\lib\cc\        cc0 cc1 cc2 and include\, the board's /usr/lib/cc
#   pc3host\share\         documentation, the man pages, the examples
#   pc3host\pc3.cmd        a command prompt with bin first on the PATH
#
# There are no symlinks in a zip and no /usr/bin on Windows, so pc3.cmd
# is the whole of the installation: run it, and cc, bcrun, mmbedit and
# the rest are on the PATH of that window.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
VER=$(tr -d '[:space:]' < "$R/VERSION")
B=$R/build-win-pkg
S=$B/stage/pc3host
OUT=$R/dist/pc3host-${VER}-win64.zip

command -v x86_64-w64-mingw32-gcc > /dev/null 2>&1 || {
	echo "make-zip: no x86_64-w64-mingw32-gcc (apt install mingw-w64)" >&2; exit 1; }
command -v zip > /dev/null 2>&1 || { echo "make-zip: no zip" >&2; exit 1; }

rm -rf "$B/stage"
mkdir -p "$B"
cmake -S "$R" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$R/cmake/toolchain-mingw64.cmake" \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/ \
      ${FUZIX_ROOT:+-DFUZIX_ROOT=$FUZIX_ROOT} > "$B/configure.log" 2>&1 \
	|| { cat "$B/configure.log"; exit 1; }
cmake --build "$B" -j"$(nproc)" 2>&1 | grep -E "error|Error" || true
DESTDIR=$S cmake --install "$B" > /dev/null

# The installer, its undo, and the PowerShell they share: they put
# <folder>\bin on this account's PATH and register .bc, which is what
# turns "specify the path to the executables" into typing cc.
cp "$R/packaging/win/install.cmd" "$R/packaging/win/uninstall.cmd" \
   "$R/packaging/win/pc3env.ps1" "$S/"
for f in install.cmd uninstall.cmd pc3env.ps1; do
	unix2dos -q "$S/$f" 2> /dev/null || sed -i 's/$/\r/' "$S/$f"
done

# The launcher.  %~dp0 is the directory this file is in, with a trailing
# backslash, so the zip works wherever it lands.
cat > "$S/pc3.cmd" <<'EOF'
@echo off
rem A command prompt with the Pico Computer 3's tools on its PATH.
rem   pc3.cmd            opens the prompt
rem   pc3.cmd cc -r x.bas   runs one command that way
set "PC3=%~dp0"
set "PATH=%PC3%bin;%PATH%"
if "%~1"=="" (
    echo Pico Computer 3 tools.  cc, bcrun, mmbc, mmbedit, pc3d are on the PATH.
    cmd /k
) else (
    %*
)
EOF
unix2dos -q "$S/pc3.cmd" 2> /dev/null || sed -i 's/$/\r/' "$S/pc3.cmd"

# What to do with it, in the window that opens.  CRLF, because it will
# be opened in Notepad.
cat > "$S/README-WINDOWS.txt" <<'EOF'
The Pico Computer 3's BASIC toolchain, for Windows
==================================================

Unpack this folder anywhere.  Then either

    install.cmd     put the tools on your PATH for good, and teach
                    Windows that a .bc file is a program.  No
                    administrator rights; uninstall.cmd undoes it.
                    OPEN A NEW COMMAND PROMPT afterwards.

or, to try it without changing anything,

    pc3.cmd         a command prompt with the tools on the PATH, for
                    as long as that window is open

Either way, no runtime is needed and deleting the folder removes the
tools.

Then:

    cc -r share\examples\gfx1.bas        a picture, in a window
    cc -r share\examples\keydemo.bas     the keyboard
    cc -r share\examples\pc3bench.bas    what each kind of thing costs
    mmbedit myprog.bas                     the editor; F2 builds and runs

cc takes a .bas or a .c file and writes a .bc beside your working
directory, and bcrun runs it; cc -r does both at once.  To run one you
built earlier:

    bcrun myprog.bc         anywhere, and the one to use in PowerShell
    .\myprog.bc             in a command prompt, note the BACKslash

A command prompt will not take ./ for a program in the current folder,
and neither shell will run a bare "myprog": a .bc is not a Windows
executable, it is a file that bcrun runs, the way a #! line has the
board run it.  install.cmd registers the extension, so a double click
in Explorer runs one - and so does ./myprog.bc in PowerShell, though
PowerShell then treats it as a document it has opened rather than a
program it is running, and will not let you pipe its output.

The examples are under share\examples, the samples the board ships
with under share\examples\samples, and the whole translator source
and its test corpus under share\mmb2c.

The window
----------
The first program to draw starts pc3d, the display server, and opens
the window; it stays up after the program ends, so the picture
survives it, as on the board.  Closing the window stops the server and
interrupts whatever program was drawing, as Ctrl-C would.  A server
started this way writes its messages to pc3d.log in your %TEMP%
folder.

Sound
-----
    bin\pcmpace 2 512 16384 10000       feeds the ring, counts underruns
    bin\sndharness snd.wav 440          writes a WAV
    cc -r share\examples\samples\playdemo.bas

Windows will ask about the firewall
-----------------------------------
A BASIC program that listens on a socket - the WEB family, websrv.bas -
makes Windows ask whether to allow it through the firewall.  Programs
talking to your own machine work whatever you answer; allow it only if
you want other machines to reach the program.

What is not here
----------------
The board's hardware: the pins, I2C, SPI, PWM, the counting inputs and
the real-time clock registers.  Those statements answer "not available"
as they do on any host, rather than pretending.
EOF
unix2dos -q "$S/README-WINDOWS.txt" 2> /dev/null || sed -i 's/$/\r/' "$S/README-WINDOWS.txt"

# A check the machine can run on itself: the eclipse predictor - 3,213
# lines of real astronomy, the translator's oldest fixture - compiled
# and run against the output it must produce, then the graphics
# example, which opens the window.  No Git Bash, no test corpus, no
# arguments: it is the answer to "did this install work".
cat > "$S/selftest.cmd" <<'EOF'
@echo off
setlocal
set "PC3=%~dp0"
set "PATH=%PC3%bin;%PATH%"
set "W=%TEMP%\pc3-selftest"
if exist "%W%" rd /s /q "%W%"
mkdir "%W%"
copy /y "%PC3%share\examples\solar_eclipse.*" "%W%" > nul
cd /d "%W%"
echo Building the eclipse predictor (3,213 lines) with cc...
cc solar_eclipse.bas > build.log 2>&1
if errorlevel 1 goto buildfail
echo Running it...
bcrun solar_eclipse.bc < solar_eclipse.in > got.txt 2>&1
findstr /v /c:"Time taken" got.txt > got.f
findstr /v /c:"Time taken" solar_eclipse.expected > want.f
fc /w got.f want.f > nul
if errorlevel 1 goto outputfail
echo   PASS  the predictor's output is right to the last digit
echo.
echo Opening the window with an example.  Close it when you have seen it.
cc -r "%PC3%share\examples\gfx1.bas"
echo.
echo Self test finished.  The work is in %W%
pause
exit /b 0
:buildfail
echo   FAIL  it did not compile.  The log:
type build.log
pause
exit /b 1
:outputfail
echo   FAIL  the output differs:
fc /w got.f want.f
pause
exit /b 1
EOF
unix2dos -q "$S/selftest.cmd" 2> /dev/null || sed -i 's/$/\r/' "$S/selftest.cmd"

mkdir -p "$R/dist"
rm -f "$OUT"
( cd "$B/stage" && zip -qr "$OUT" pc3host )
echo "$OUT"
ls -l "$OUT" | awk '{print $5, "bytes"}'
unzip -l "$OUT" | tail -1
