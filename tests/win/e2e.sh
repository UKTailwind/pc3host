#!/bin/bash
#
# The display, keyboard and sound gates on Windows: tests/e2e/run.sh's
# programs and goldens against a headless pc3d.exe.
#
#   bash tests/win/e2e.sh <tree> <pc3host> <work>
#
# from Git Bash: <tree> holds bin/ and lib/cc/, <pc3host> is this
# repository (for tests/e2e), <work> a directory of its own.  The socket
# is a short fixed path under %TEMP%: an AF_UNIX path is at most 108
# characters on Windows as everywhere.
T=$1; P=$2; W=$3
[ -x "$T/bin/pc3d.exe" ] && [ -d "$P/tests/e2e" ] && [ -n "$W" ] || {
	echo "usage: e2e.sh <tree> <pc3host> <work>" >&2; exit 2; }
D=$P/tests/e2e
rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
cp "$D"/gfx1.bas "$D"/gfxc.c "$D"/keys.bas "$D"/play.bas "$D"/*.expected "$D"/*.golden.bmp .
export PC3_SOCKET="$(cygpath -w "$TEMP")\\pc3-e2e.sock"
export PC3_AUTOSTART=0
rm -f "$TEMP/pc3-e2e.sock"
"$T/bin/pc3d.exe" --headless --audio null --socket "$PC3_SOCKET" > pc3d.log 2>&1 &
SRV=$!
sleep 1.5
fail=0
check() { if cmp -s "$2" "$3"; then echo "pass  $1"; else echo "FAIL  $1"; fail=1; fi; }
"$T/bin/cc.exe" gfx1.bas > gfx1.cc 2>&1 || { echo "FAIL  cc gfx1"; cat gfx1.cc; fail=1; }
timeout 60 "$T/bin/bcrun.exe" gfx1.bc > gfx1.stdout 2>&1
check "gfx1 readback" gfx1.out gfx1.expected
timeout 60 "$T/bin/saveimage.exe" gfx1.bmp > /dev/null 2>&1 || { echo "FAIL  saveimage gfx1"; fail=1; }
check "gfx1 screen" gfx1.bmp gfx1.golden.bmp
"$T/bin/cc.exe" gfxc.c > gfxc.cc 2>&1 || { echo "FAIL  cc gfxc"; cat gfxc.cc; fail=1; }
timeout 60 "$T/bin/bcrun.exe" gfxc.bc > gfxc.out 2>&1
check "gfxc output" gfxc.out gfxc.expected
timeout 60 "$T/bin/saveimage.exe" gfxc.bmp > /dev/null 2>&1 || { echo "FAIL  saveimage gfxc"; fail=1; }
check "gfxc screen" gfxc.bmp gfxc.golden.bmp
"$T/bin/cc.exe" keys.bas > keys.cc 2>&1 || { echo "FAIL  cc keys"; fail=1; }
( timeout 30 "$T/bin/bcrun.exe" keys.bc < /dev/null > keys.stdout 2>&1 ) &
sleep 1.5
K=$T/bin/pc3key.exe
$K press A; sleep 0.1; $K release A; sleep 0.2; $K tap UP; sleep 0.2
$K press LEFT_SHIFT; $K tap B; $K release LEFT_SHIFT; sleep 0.2; $K tap Q
sleep 2
check "keys readback" keys.out keys.expected
timeout 60 "$T/bin/pcmpace.exe" 2 512 16384 10000 > pcmpace.out 2>&1
if grep -q "underruns while feeding: 0" pcmpace.out; then echo "pass  pcmpace"; else echo "FAIL  pcmpace"; tail -4 pcmpace.out; fail=1; fi
"$T/bin/sndharness.exe" snd.wav 440 > /dev/null 2>&1
"$T/bin/cc.exe" play.bas > play.cc 2>&1 || { echo "FAIL  cc play"; fail=1; }
timeout 60 "$T/bin/bcrun.exe" play.bc > play.out 2>&1
check "play" play.out play.expected
kill $SRV 2>/dev/null; sleep 1
exit $fail
