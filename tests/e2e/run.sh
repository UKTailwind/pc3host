#!/bin/bash
#
# The end-to-end gate for the display server: a BASIC program and a C
# program run against a headless pc3d through the real chain, and what
# they read back and what the screen holds are compared with goldens.
#
#   bash tests/e2e/run.sh <bin dir> <FUZIX root> <work dir> [bless]
#
# Two programs, two rasters:
#
#   gfx1.bas  MODE 2 (320x240, 16 colours) through mmbc and the runtime
#             inside bcrun.  Its PIXEL() readbacks go to gfx1.out; the
#             screen is then taken with saveimage - a separate process,
#             which is the point: the picture survives the program, as
#             it does on the board.
#   gfxc.c    MODE 0 (640x256, two colours) as a C program for the PC3,
#             compiled by cc to bytecode, so its ioctls cross bcrun's
#             libcalls and the ILP32 translation in host_sys_ioctl.
#
# `bless` rewrites the goldens from this run.  Look at the PNGs first.

BIN=$1
FZ=$2
W=$3
BLESS=$4
D=$(cd "$(dirname "$0")" && pwd)
M=$FZ/Applications/mmb2c
export PC3_SOCKET=$W/pc3d.sock
export PC3_AUTOSTART=0
unset PC3_DISPLAY

fail=0
die() { echo "e2e: $*" >&2; exit 1; }

[ -x "$BIN/pc3d" ] || die "no $BIN/pc3d"
[ -x "$BIN/bcrun" ] || die "no $BIN/bcrun"
[ -x "$BIN/saveimage" ] || die "no $BIN/saveimage"
rm -rf "$W"
mkdir -p "$W" || die "cannot make $W"

"$BIN/pc3d" --headless --audio null --socket "$PC3_SOCKET" &
SRV=$!
trap 'kill $SRV 2>/dev/null; wait $SRV 2>/dev/null' EXIT
for i in $(seq 1 50); do
	[ -S "$PC3_SOCKET" ] && break
	sleep 0.1
done
[ -S "$PC3_SOCKET" ] || die "server did not come up"

check() {			# check <name> <got> <golden>
	if [ -n "$BLESS" ]; then
		cp "$2" "$3" && echo "blessed $3"
		return
	fi
	if cmp -s "$2" "$3"; then
		echo "pass  $1"
	else
		echo "FAIL  $1: $2 differs from $3"
		[ -f "$3" ] && diff "$3" "$2" | head -8
		fail=1
	fi
}

# ---- gfx1.bas: MODE 2 through mmbc -------------------------------------------
# fccbuild.sh honours BIN (this build's cc0/cc1/cc2) and W, and takes
# the translator from MMB2C.  It is run FROM the work directory: cc2
# writes .symtmp where it stands, and that belongs here, not wherever
# the caller was.
if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/gfx1.bas" ) > "$W/gfx1.build.log" 2>&1; then
	echo "FAIL  gfx1 (build)"; tail -5 "$W/gfx1.build.log"; fail=1
else
	( cd "$W" && "$BIN/bcrun" "$W/gfx1.bc" > "$W/gfx1.stdout" 2>&1 )
	echo "bcrun gfx1 exit $?" >> "$W/gfx1.stdout"
	check "gfx1 readback" "$W/gfx1.out" "$D/gfx1.expected"
	( cd "$W" && "$BIN/saveimage" "$W/gfx1.bmp" ) || { echo "FAIL  gfx1 saveimage"; fail=1; }
	python3 "$D/bmp2png.py" "$W/gfx1.bmp" "$W/gfx1.png" > /dev/null
	check "gfx1 screen" "$W/gfx1.bmp" "$D/gfx1.golden.bmp"
fi

# ---- writebuff.bas: the framebuffer's bytes are a PicoMite's -----------------
# PEEK, POKE and MEMORY COPY at MM.INFO(WRITEBUFF), in MODE 2 and MODE 1.
# The golden is worked from the PicoMite's DrawPixel16 and DrawPixel2
# (the program's header), not blessed from a run.
if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/writebuff.bas" ) > "$W/writebuff.build.log" 2>&1; then
	echo "FAIL  writebuff (build)"; tail -5 "$W/writebuff.build.log"; fail=1
else
	( cd "$W" && "$BIN/bcrun" "$W/writebuff.bc" < /dev/null > "$W/writebuff.stdout" 2>&1 )
	check "writebuff bytes" "$W/writebuff.out" "$D/writebuff.expected"
fi

# ---- warp_test.bas, affine_test.bas: MMBasic V8's raster commands -------------
# The PicoMite's own Bas/bench tests, unmodified: FRAMEBUFFER WARPH and
# WARPV, then AFFINE and SPAN, every case checked with PIXEL() against
# the program's own model of the command, in MODE 2 and MODE 3 (MODE 4
# and 5 report that this machine has no such mode), then each error by
# MM.ERRMSG$.  They draw with framebuffer 2 and name T, as the VGA and
# HDMI firmware has them.  The core is checked pixel for pixel by
# tests/seam/warp_core.c; this is the whole chain.  RND is the hardware
# generator's here, as on an RP2350 (RANDOMIZE is a no-op), so the cases
# differ from run to run: how many of affine_test's were AFFINE and how
# many SPAN is the one number that does, and it is not compared.
for t in warp_test affine_test; do
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/$t.bas" ) > "$W/$t.build.log" 2>&1; then
		echo "FAIL  $t (build)"; tail -5 "$W/$t.build.log"; fail=1
	else
		( cd "$W" && "$BIN/bcrun" "$W/$t.bc" < /dev/null > "$W/$t.out" 2>&1 )
		sed -E 's/:[0-9]+ AFFINE and[0-9]+ SPAN cases/:n AFFINE and n SPAN cases/' \
			"$W/$t.out" > "$W/$t.cmp"
		check "$t" "$W/$t.cmp" "$D/$t.expected"
	fi
done

# ---- tilemap_load.bas: TILEMAP LOAD ------------------------------------------
# The PicoMite's own Testfiles/TilemapLoadTest.bas, unmodified, with the
# Bas/breakout.map it reads: a map from a file against the same map from
# DATA, the default extension, a reload, every malformed file by its
# message, and a 256 x 256 map.  How long that took is not compared.
cp "$D/breakout.map" "$W/"
if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/tilemap_load.bas" ) > "$W/tilemap_load.build.log" 2>&1; then
	echo "FAIL  tilemap_load (build)"; tail -5 "$W/tilemap_load.build.log"; fail=1
else
	( cd "$W" && "$BIN/bcrun" "$W/tilemap_load.bc" < /dev/null > "$W/tilemap_load.out" 2>&1 )
	sed -E 's/load took[0-9]+ ms/load took n ms/' "$W/tilemap_load.out" > "$W/tilemap_load.cmp"
	check "tilemap_load" "$W/tilemap_load.cmp" "$D/tilemap_load.expected"
fi

# ---- pngcrop.bas: LOAD PNG's crop and offsets, SPRITE LOADPNG's part --------
# big.png is 400 x 300, bigger than MODE 2's screen, every pixel one of
# the RGB121 colours PIXEL() reads back; the program checks the screen
# against its own model of the picture.  loadpng's message for an
# offset outside the image is part of what is compared.
cp "$D/big.png" "$W/"
if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/pngcrop.bas" ) > "$W/pngcrop.build.log" 2>&1; then
	echo "FAIL  pngcrop (build)"; tail -5 "$W/pngcrop.build.log"; fail=1
else
	( cd "$W" && "$BIN/bcrun" "$W/pngcrop.bc" < /dev/null > "$W/pngcrop.out" 2>&1 )
	check "pngcrop" "$W/pngcrop.out" "$D/pngcrop.expected"
fi

# ---- spritefix.bas: the PicoMite's sprite fixes, by their pixels -----------
# SWAP over the stored background (#89), WRITE as a stamp and the opaque
# orientations kept with the sprite (#90), and a BMP that fails to
# decode leaving no buffer in use (#93; bad16.bmp is good16.bmp cut off
# after three rows).  loadimage's message for it is compared too.
cp "$D/good16.bmp" "$D/bad16.bmp" "$W/"
if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/spritefix.bas" ) > "$W/spritefix.build.log" 2>&1; then
	echo "FAIL  spritefix (build)"; tail -5 "$W/spritefix.build.log"; fail=1
else
	( cd "$W" && "$BIN/bcrun" "$W/spritefix.bc" < /dev/null > "$W/spritefix.out" 2>&1 )
	check "spritefix" "$W/spritefix.out" "$D/spritefix.expected"
fi
# SPRITE(B): the background collision and its sides, the expected values
# worked by hand from the firmware's fun_sprite (issue #92's case among them)
if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/spriteb.bas" ) > "$W/spriteb.build.log" 2>&1; then
	echo "FAIL  spriteb (build)"; tail -5 "$W/spriteb.build.log"; fail=1
else
	( cd "$W" && "$BIN/bcrun" "$W/spriteb.bc" < /dev/null > "$W/spriteb.out" 2>&1 )
	check "spriteb" "$W/spriteb.out" "$D/spriteb.expected"
fi

# ---- serial.bas: a serial port, here a pseudo-terminal -----------------------
# OPEN "port:baud,buffer,interrupt,level" AS #n with the port named as the
# OS names it.  serialpty.py links one end of a pty as ./ttyTEST and
# answers at the other: LINE INPUT, LOC, INPUT$, EOF, LOF, the interrupt
# at its level and CLOSE.  What the program saw is serial.out; what it
# sent, the transcript.  Linux only: Windows has no pty, and its COM code
# is compiled, not run, here.
if command -v python3 > /dev/null; then
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/serial.bas" ) > "$W/serial.build.log" 2>&1; then
		echo "FAIL  serial (build)"; tail -5 "$W/serial.build.log"; fail=1
	elif ! python3 "$D/serialpty.py" "$W" "$BIN/bcrun" "$W/serial.bc" > "$W/serial.pty" 2>&1; then
		echo "FAIL  serial: $(tail -1 "$W/serial.pty")"; fail=1
	else
		check "serial" "$W/serial.out" "$D/serial.expected"
		check "serial sent" "$W/serial.pty" "$D/serial_pty.expected"
	fi
fi

# ---- threads.bas: INPUT lets the other threads run -------------------------
# The line comes 0.3 s after the program starts, through a pipe: the
# busy thread and the other thread's SETTICK must have run meanwhile,
# where a blocking read would have stopped them both.
if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/threads.bas" ) > "$W/threads.build.log" 2>&1; then
	echo "FAIL  threads (build)"; tail -5 "$W/threads.build.log"; fail=1
else
	( cd "$W" && ( sleep 0.3; echo hello ) | timeout 20 "$BIN/bcrun" "$W/threads.bc" > "$W/threads.out" 2>&1 )
	check "threads" "$W/threads.out" "$D/threads.expected"
fi

# ---- layers.bas, layers_tft.bas: the layers as the window shows them -----------
# Framebuffer 2 MERGEd over F, and L and T shown live, T over L, as the
# VGA and HDMI firmware shows them; then the TFT model, where a MERGE of
# the layer stops it being shown live.  The pixels are read from the
# window pc3d presented as each program left, and the colours expected
# are worked from the rules (each program's header), not blessed.  A
# server of their own, with --snapshot: it writes the window at every
# disconnect, which would slow the key tests' server down.
LSOCK=$W/layers.sock
"$BIN/pc3d" --headless --audio null --scale 1 --snapshot "$W/snap.ppm" \
	--socket "$LSOCK" &
LSRV=$!
for i in $(seq 1 50); do
	[ -S "$LSOCK" ] && break
	sleep 0.1
done
for t in layers layers_tft; do
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/$t.bas" ) > "$W/$t.build.log" 2>&1; then
		echo "FAIL  $t (build)"; tail -5 "$W/$t.build.log"; fail=1
	else
		rm -f "$W/snap.ppm"
		( cd "$W" && PC3_SOCKET=$LSOCK "$BIN/bcrun" "$W/$t.bc" < /dev/null > "$W/$t.stdout" 2>&1 )
		for i in $(seq 1 50); do	# the snapshot is written as the server sees it go
			[ -s "$W/snap.ppm" ] && break
			sleep 0.1
		done
		python3 "$D/ppmpix.py" "$W/snap.ppm" 1 $(cut -d' ' -f1 "$D/$t.expected") \
			> "$W/$t.out" 2>&1
		check "$t pixels" "$W/$t.out" "$D/$t.expected"
	fi
done
kill $LSRV 2>/dev/null
wait $LSRV 2>/dev/null

# ---- gfxc.c: MODE 0 from C, through bcrun's libcalls --------------------------
# The program includes pico_ioctl.h; fccbuild's include path has the
# work directory in it, so the header goes there.
cp "$FZ/Kernel/platform/platform-rpipico/pico_ioctl.h" "$W/"
if ! ( cd "$W" && BIN=$BIN W=$W bash "$M/fcc/fccbuild.sh" "$D/gfxc.c" ) > "$W/gfxc.build.log" 2>&1; then
	echo "FAIL  gfxc (build)"; tail -5 "$W/gfxc.build.log"; fail=1
else
	( cd "$W" && "$BIN/bcrun" "$W/gfxc.bc" > "$W/gfxc.out" 2>&1 )
	check "gfxc output" "$W/gfxc.out" "$D/gfxc.expected"
	( cd "$W" && "$BIN/saveimage" "$W/gfxc.bmp" ) || { echo "FAIL  gfxc saveimage"; fail=1; }
	python3 "$D/bmp2png.py" "$W/gfxc.bmp" "$W/gfxc.png" > /dev/null
	check "gfxc screen" "$W/gfxc.bmp" "$D/gfxc.golden.bmp"
fi

# ---- keys.bas: the window's keyboard, pressed by pc3key --------------------------
# The program has no terminal (stdin is /dev/null), so its INKEY$ and
# KEYDOWN can only be fed by the server.  It starts, opens its key
# channel on the first INKEY$, and then the script presses keys: a held
# 'a' (read back through KEYDOWN while down), the up arrow (MMBasic
# code 128), shift-b (66), and q to finish.  The 'a' is held for a
# tenth of a second: the decoder repeats a key after 250 ms, and a
# repeat is a second 'a', which the first draft of this test got.
if [ -x "$BIN/pc3key" ]; then
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/keys.bas" ) > "$W/keys.build.log" 2>&1; then
		echo "FAIL  keys (build)"; tail -5 "$W/keys.build.log"; fail=1
	else
		( cd "$W" && "$BIN/bcrun" "$W/keys.bc" < /dev/null > "$W/keys.stdout" 2>&1 ) &
		PROG=$!
		sleep 1
		"$BIN/pc3key" press A;  sleep 0.1
		"$BIN/pc3key" release A; sleep 0.2
		"$BIN/pc3key" tap UP; sleep 0.2
		"$BIN/pc3key" press LEFT_SHIFT; "$BIN/pc3key" tap B; "$BIN/pc3key" release LEFT_SHIFT; sleep 0.2
		"$BIN/pc3key" tap Q
		for i in $(seq 1 50); do
			kill -0 $PROG 2>/dev/null || break
			sleep 0.1
		done
		if kill -0 $PROG 2>/dev/null; then
			echo "FAIL  keys: program did not finish"; kill $PROG; fail=1
		fi
		wait $PROG 2>/dev/null
		check "keys readback" "$W/keys.out" "$D/keys.expected"
	fi
fi

# ---- prtscr.bas: OPTION PRINTSCREEN, Print Screen pressed by pc3key --------------
# Once in each of the program's four two-second phases, counted from
# when its ref.bmp appears (the program's header says what each is).
# Three presses with the option on must leave two SCREEN_*.BMP and one
# SCREEN_*.PNG, each the picture ref.bmp is, and never reach the
# program; the fourth, with it off, is read as 157.  It runs in a
# directory of its own, which is where the screenshots go.
if [ -x "$BIN/pc3key" ]; then
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/prtscr.bas" ) > "$W/prtscr.build.log" 2>&1; then
		echo "FAIL  prtscr (build)"; tail -5 "$W/prtscr.build.log"; fail=1
	else
		P=$W/prtscr
		mkdir -p "$P"
		( cd "$P" && "$BIN/bcrun" "$W/prtscr.bc" < /dev/null > "$W/prtscr.stdout" 2>&1 ) &
		PROG=$!
		for i in $(seq 1 100); do
			[ -f "$P/ref.bmp" ] && break
			sleep 0.1
		done
		sleep 1;  "$BIN/pc3key" tap PRINT_SCREEN
		sleep 2;  "$BIN/pc3key" tap PRINT_SCREEN
		sleep 2;  "$BIN/pc3key" tap PRINT_SCREEN
		sleep 2;  "$BIN/pc3key" tap PRINT_SCREEN
		for i in $(seq 1 50); do
			kill -0 $PROG 2>/dev/null || break
			sleep 0.1
		done
		if kill -0 $PROG 2>/dev/null; then
			echo "FAIL  prtscr: program did not finish"; kill $PROG; fail=1
		fi
		wait $PROG 2>/dev/null
		check "prtscr keys" "$P/prtscr.out" "$D/prtscr.expected"
		nb=$(ls "$P"/SCREEN_*.BMP 2>/dev/null | wc -l)
		np=$(ls "$P"/SCREEN_*.PNG 2>/dev/null | wc -l)
		if [ "$nb" = 2 ] && [ "$np" = 1 ]; then
			echo "pass  prtscr: two BMPs and a PNG"
		else
			echo "FAIL  prtscr: $nb BMPs and $np PNGs, not 2 and 1"; fail=1
		fi
		for s in "$P"/SCREEN_*; do
			[ -f "$s" ] || continue
			if python3 "$D/bmp2png.py" --same "$s" "$P/ref.bmp"; then
				echo "pass  prtscr: $(basename "$s") is the screen"
			else
				echo "FAIL  prtscr: $(basename "$s")"; fail=1
			fi
		done
	fi
fi

# ---- onkeyarm.bas: ON KEY k counts only a k that comes while it is armed ---------
# A at +0.5 s (before ON KEY 97 is armed at +1.5 s) and at +2.5 s, from
# when the program's "start" file appears; the program's header says
# what each must do.
if [ -x "$BIN/pc3key" ]; then
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/onkeyarm.bas" ) > "$W/onkeyarm.build.log" 2>&1; then
		echo "FAIL  onkeyarm (build)"; tail -5 "$W/onkeyarm.build.log"; fail=1
	else
		K=$W/onkeyarm
		mkdir -p "$K"
		( cd "$K" && "$BIN/bcrun" "$W/onkeyarm.bc" < /dev/null > "$W/onkeyarm.stdout" 2>&1 ) &
		PROG=$!
		for i in $(seq 1 100); do
			[ -f "$K/start" ] && break
			sleep 0.1
		done
		sleep 0.5; "$BIN/pc3key" tap A
		sleep 2;   "$BIN/pc3key" tap A
		for i in $(seq 1 50); do
			kill -0 $PROG 2>/dev/null || break
			sleep 0.1
		done
		if kill -0 $PROG 2>/dev/null; then
			echo "FAIL  onkeyarm: program did not finish"; kill $PROG; fail=1
		fi
		wait $PROG 2>/dev/null
		check "onkeyarm" "$K/onkeyarm.out" "$D/onkeyarm.expected"
	fi
fi

# ---- sound: the PCM ring paced by pcmpace, and PLAY through BASIC ----------------
# The server runs miniaudio's null backend, which consumes frames in
# real time and plays nothing.  pcmpace feeds a 440 Hz sine at playsnd's
# old pattern (512-frame chunks to a 16K target every 10 ms) and reports
# the underruns; 16K is 93 ms of audio, so the drain afterwards is a
# handful of 20 ms polls.  play.bas then does what a program does.
if [ -x "$BIN/pcmpace" ]; then
	"$BIN/pcmpace" 2 512 16384 10000 > "$W/pcmpace.out" 2>&1
	# "while feeding" is the number: the final count adds the empty
	# blocks between the ring running dry and the program's next 20 ms
	# poll noticing, on the board as here.
	if grep -q "underruns while feeding: 0" "$W/pcmpace.out"; then
		echo "pass  pcmpace: no underruns while feeding"
	else
		echo "FAIL  pcmpace"; tail -4 "$W/pcmpace.out"; fail=1
	fi
	polls=$(sed -n 's/drained after \([0-9]*\) polls.*/\1/p' "$W/pcmpace.out")
	if [ -n "$polls" ] && [ "$polls" -le 12 ]; then
		echo "pass  pcmpace: 16K drained in $polls polls"
	else
		echo "FAIL  pcmpace: drain took ${polls:-?} polls"; fail=1
	fi
fi
if [ -x "$BIN/sndharness" ]; then
	"$BIN/sndharness" "$W/snd.wav" 440 > /dev/null
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/play.bas" ) > "$W/play.build.log" 2>&1; then
		echo "FAIL  play (build)"; tail -5 "$W/play.build.log"; fail=1
	else
		( cd "$W" && timeout 30 "$BIN/bcrun" "$W/play.bc" < /dev/null > "$W/play.stdout" 2>&1 )
		echo "bcrun play exit $?" >> "$W/play.stdout"
		check "play readback" "$W/play.out" "$D/play.expected"
	fi
	# PLAY MODSAMPLE: what playmod asks hxcmod to do, from its trace
	cp "$D/doppler.mod" "$W/"
	rm -f "$W/modsample.calls"
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$D/modsample.bas" ) > "$W/modsample.build.log" 2>&1; then
		echo "FAIL  modsample (build)"; tail -5 "$W/modsample.build.log"; fail=1
	else
		( cd "$W" && PLAYMOD_TRACE="$W/modsample.calls" timeout 30 "$BIN/bcrun" "$W/modsample.bc" < /dev/null > "$W/modsample.stdout" 2>&1 )
		check "modsample readback" "$W/modsample.out" "$D/modsample.expected"
		check "modsample calls" "$W/modsample.calls" "$D/modsample_calls.expected"
	fi
fi

echo "e2e: pictures in $W/gfx1.png and $W/gfxc.png"
exit $fail
