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
cp "$D"/gfx1.bas "$D"/gfxc.c "$D"/keys.bas "$D"/play.bas "$D"/writebuff.bas "$D"/prtscr.bas "$D"/onkeyarm.bas "$D"/*.expected "$D"/*.golden.bmp .
cp "$D"/warp_test.bas "$D"/affine_test.bas "$D"/layers.bas "$D"/layers_tft.bas .
cp "$D"/modsample.bas "$D"/doppler.mod "$D"/tilemap_load.bas "$D"/breakout.map .
cp "$D"/pngcrop.bas "$D"/big.png .
cp "$D"/spritefix.bas "$D"/good16.bmp "$D"/bad16.bmp "$D"/spriteb.bas .
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
"$T/bin/cc.exe" writebuff.bas > writebuff.cc 2>&1 || { echo "FAIL  cc writebuff"; cat writebuff.cc; fail=1; }
timeout 60 "$T/bin/bcrun.exe" writebuff.bc < /dev/null > writebuff.stdout 2>&1
check "writebuff bytes" writebuff.out writebuff.expected
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
# OPTION PRINTSCREEN: tests/e2e/run.sh says what the four presses are.
# The pictures are compared by bmp2png.py, through py -3: Git Bash's
# python3 is the Store's stub.
PY="py -3"; command -v py > /dev/null || PY=python
"$T/bin/cc.exe" prtscr.bas > prtscr.cc 2>&1 || { echo "FAIL  cc prtscr"; fail=1; }
mkdir -p prtscr
( cd prtscr && timeout 30 "$T/bin/bcrun.exe" ../prtscr.bc < /dev/null > ../prtscr.stdout 2>&1 ) &
for i in $(seq 1 100); do [ -f prtscr/ref.bmp ] && break; sleep 0.1; done
sleep 1; $K tap PRINT_SCREEN; sleep 2; $K tap PRINT_SCREEN
sleep 2; $K tap PRINT_SCREEN; sleep 2; $K tap PRINT_SCREEN
sleep 3
check "prtscr keys" prtscr/prtscr.out prtscr.expected
nb=$(ls prtscr/SCREEN_*.BMP 2>/dev/null | wc -l); np=$(ls prtscr/SCREEN_*.PNG 2>/dev/null | wc -l)
if [ "$nb" = 2 ] && [ "$np" = 1 ]; then echo "pass  prtscr: two BMPs and a PNG"; else echo "FAIL  prtscr: $nb BMPs, $np PNGs"; fail=1; fi
for s in prtscr/SCREEN_*; do
	[ -f "$s" ] || continue
	if $PY "$(cygpath -w "$D/bmp2png.py")" --same "$s" prtscr/ref.bmp; then echo "pass  prtscr: $(basename "$s")"; else echo "FAIL  prtscr: $(basename "$s")"; fail=1; fi
done
# ON KEY k counts only a k that comes while it is armed (onkeyarm.bas)
"$T/bin/cc.exe" onkeyarm.bas > onkeyarm.cc 2>&1 || { echo "FAIL  cc onkeyarm"; fail=1; }
mkdir -p onkeyarm
( cd onkeyarm && timeout 30 "$T/bin/bcrun.exe" ../onkeyarm.bc < /dev/null > ../onkeyarm.stdout 2>&1 ) &
for i in $(seq 1 100); do [ -f onkeyarm/start ] && break; sleep 0.1; done
sleep 0.5; $K tap A; sleep 2; $K tap A
sleep 2.5
check "onkeyarm" onkeyarm/onkeyarm.out onkeyarm.expected
timeout 60 "$T/bin/pcmpace.exe" 2 512 16384 10000 > pcmpace.out 2>&1
if grep -q "underruns while feeding: 0" pcmpace.out; then echo "pass  pcmpace"; else echo "FAIL  pcmpace"; tail -4 pcmpace.out; fail=1; fi
"$T/bin/sndharness.exe" snd.wav 440 > /dev/null 2>&1
"$T/bin/cc.exe" play.bas > play.cc 2>&1 || { echo "FAIL  cc play"; fail=1; }
timeout 60 "$T/bin/bcrun.exe" play.bc > play.out 2>&1
check "play" play.out play.expected
# PLAY MODSAMPLE: what playmod asked hxcmod to do, from its trace
"$T/bin/cc.exe" modsample.bas > modsample.cc 2>&1 || { echo "FAIL  cc modsample"; fail=1; }
PLAYMOD_TRACE="$(cygpath -w "$W")\\modsample.calls" timeout 60 "$T/bin/bcrun.exe" modsample.bc < /dev/null > modsample.stdout 2>&1
check "modsample" modsample.out modsample.expected
tr -d '\r' < modsample.calls > modsample.calls.cmp
check "modsample calls" modsample.calls.cmp modsample_calls.expected
# MMBasic V8's raster commands: tests/e2e/run.sh says what these are and
# why the AFFINE and SPAN counts are not compared
for t in warp_test affine_test; do
	"$T/bin/cc.exe" $t.bas > $t.cc 2>&1 || { echo "FAIL  cc $t"; cat $t.cc; fail=1; }
	timeout 120 "$T/bin/bcrun.exe" $t.bc < /dev/null > $t.out 2>&1
	# Git Bash's sed drops the CRs, so they are taken out of both
	tr -d '\r' < $t.out | sed -E 's/:[0-9]+ AFFINE and[0-9]+ SPAN cases/:n AFFINE and n SPAN cases/' > $t.cmp
	tr -d '\r' < $t.expected > $t.exp
	check "$t" $t.cmp $t.exp
done
# TILEMAP LOAD: the PicoMite's own test, as run.sh runs it
"$T/bin/cc.exe" tilemap_load.bas > tilemap_load.cc 2>&1 || { echo "FAIL  cc tilemap_load"; cat tilemap_load.cc; fail=1; }
timeout 120 "$T/bin/bcrun.exe" tilemap_load.bc < /dev/null > tilemap_load.out 2>&1
tr -d '\r' < tilemap_load.out | sed -E 's/load took[0-9]+ ms/load took n ms/' > tilemap_load.cmp
tr -d '\r' < tilemap_load.expected > tilemap_load.exp
check "tilemap_load" tilemap_load.cmp tilemap_load.exp
# LOAD PNG's crop and offsets, SPRITE LOADPNG's part, as run.sh runs it
"$T/bin/cc.exe" pngcrop.bas > pngcrop.cc 2>&1 || { echo "FAIL  cc pngcrop"; cat pngcrop.cc; fail=1; }
timeout 60 "$T/bin/bcrun.exe" pngcrop.bc < /dev/null > pngcrop.out 2>&1
tr -d '\r' < pngcrop.out > pngcrop.cmp
tr -d '\r' < pngcrop.expected > pngcrop.exp
check "pngcrop" pngcrop.cmp pngcrop.exp
# the PicoMite's sprite fixes, by their pixels, as run.sh runs it
"$T/bin/cc.exe" spritefix.bas > spritefix.cc 2>&1 || { echo "FAIL  cc spritefix"; cat spritefix.cc; fail=1; }
timeout 60 "$T/bin/bcrun.exe" spritefix.bc < /dev/null > spritefix.out 2>&1
tr -d '\r' < spritefix.out > spritefix.cmp
tr -d '\r' < spritefix.expected > spritefix.exp
check "spritefix" spritefix.cmp spritefix.exp
"$T/bin/cc.exe" spriteb.bas > spriteb.cc 2>&1 || { echo "FAIL  cc spriteb"; cat spriteb.cc; fail=1; }
timeout 60 "$T/bin/bcrun.exe" spriteb.bc < /dev/null > spriteb.out 2>&1
tr -d '\r' < spriteb.out > spriteb.cmp
tr -d '\r' < spriteb.expected > spriteb.exp
check "spriteb" spriteb.cmp spriteb.exp
kill $SRV 2>/dev/null; sleep 1
# The live layers, read from the window a server of their own presented
# (--snapshot), as run.sh does
export PC3_SOCKET="$(cygpath -w "$TEMP")\\pc3-e2e-layers.sock"
rm -f "$TEMP/pc3-e2e-layers.sock"
"$T/bin/pc3d.exe" --headless --audio null --scale 1 --snapshot snap.ppm \
	--socket "$PC3_SOCKET" > pc3d-layers.log 2>&1 &
SRV=$!
sleep 1.5
for t in layers layers_tft; do
	"$T/bin/cc.exe" $t.bas > $t.cc 2>&1 || { echo "FAIL  cc $t"; cat $t.cc; fail=1; }
	rm -f snap.ppm
	timeout 60 "$T/bin/bcrun.exe" $t.bc < /dev/null > $t.stdout 2>&1
	for i in $(seq 1 50); do [ -s snap.ppm ] && break; sleep 0.1; done
	$PY "$(cygpath -w "$D/ppmpix.py")" snap.ppm 1 $(cut -d' ' -f1 $t.expected | tr -d '\r') > $t.out 2>&1
	if diff -q <(tr -d '\r' < $t.out) <(tr -d '\r' < $t.expected) > /dev/null; then
		echo "pass  $t pixels"
	else
		echo "FAIL  $t pixels"; fail=1
	fi
done
kill $SRV 2>/dev/null; sleep 1
exit $fail
