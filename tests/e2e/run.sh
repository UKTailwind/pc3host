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

"$BIN/pc3d" --headless --socket "$PC3_SOCKET" &
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

echo "e2e: pictures in $W/gfx1.png and $W/gfxc.png"
exit $fail
