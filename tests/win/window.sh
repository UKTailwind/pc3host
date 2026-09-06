#!/bin/bash
#
# A real window on this desktop: the presenter thread, MiniFB on Win32,
# miniaudio on WASAPI.  The server is started with a window and the
# default audio device, a program draws into it, PLAY makes a sound, and
# the snapshot the server writes when the program leaves is checked to
# be a picture.  Run tests/win/e2e.sh first: this takes its gfx1.bc,
# play.bc and snd.wav from that work directory.
#
#   bash tests/win/window.sh <tree> <e2e-work> <work>
T=$1; E=$2; W=$3
[ -x "$T/bin/pc3d.exe" ] && [ -f "$E/gfx1.bc" ] && [ -n "$W" ] || {
	echo "usage: window.sh <tree> <e2e-work> <work>" >&2; exit 2; }
rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
cp "$E"/gfx1.bc "$E"/play.bc "$E"/snd.wav .
export PC3_SOCKET="$(cygpath -w "$TEMP")\\pc3-window.sock"
export PC3_AUTOSTART=0
rm -f "$TEMP/pc3-window.sock"
"$T/bin/pc3d.exe" --verbose --snapshot "$(cygpath -w "$W")\\snap.ppm" --socket "$PC3_SOCKET" > pc3d.log 2>&1 &
SRV=$!
sleep 2
fail=0
timeout 60 "$T/bin/bcrun.exe" gfx1.bc > gfx1.stdout 2>&1 || { echo "FAIL  gfx1 in the window"; fail=1; }
sleep 1
if [ -f snap.ppm ]; then
	n=$(python -c "d=open('snap.ppm','rb').read(); i=d.index(b'255\n')+4; px=d[i:]; print(sum(1 for k in range(0,len(px),3) if px[k]|px[k+1]|px[k+2]))" 2>/dev/null)
	if [ -n "$n" ] && [ "$n" -gt 1000 ]; then echo "pass  window snapshot: $n lit pixels"; else echo "FAIL  window snapshot"; fail=1; fi
else
	echo "FAIL  no snapshot"; fail=1
fi
timeout 60 "$T/bin/bcrun.exe" play.bc > play.out 2>&1 || { echo "FAIL  play in the window"; fail=1; }
grep -q "wav twice" play.out && echo "pass  play through the device" || { echo "FAIL  play"; cat play.out; fail=1; }
sleep 1
kill $SRV 2>/dev/null; sleep 1
grep "listening on" pc3d.log
exit $fail
