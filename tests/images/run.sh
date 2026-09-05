#!/bin/bash
#
# The image gate: the SAVE IMAGE / LOAD IMAGE round-trip tests from the
# FUZIX tree, run against a headless server, their output compared with
# goldens kept here.  Each test draws, saves through saveimage, loads
# through loadimage and reports `sum` of the files, so the goldens pin
# the exact bytes both programs produce on a PC: the modes' packing, the
# palette resolution, the BMP writer and the BMP reader.  Two of the
# tree's image tests need pictures that are not in the tree (tiger.bmp,
# the g*.bmp set) and are not here.
#
# The goldens were blessed with the GETPIXEL saveimage and reproduced by
# the rows-out one, which is how that rewrite was proved.
#
#   bash tests/images/run.sh <bin dir> <FUZIX root> <work dir> [bless]

BIN=$1
FZ=$2
W=$3
BLESS=$4
D=$(cd "$(dirname "$0")" && pwd)
M=$FZ/Applications/mmb2c
export PC3_SOCKET=$W/img.sock
export PC3_AUTOSTART=0
unset PC3_DISPLAY

TESTS="imgtrip imgloop wtest rtest imgm1 imgm1b imgm1c imgm1d saveimg"

fail=0
die() { echo "images: $*" >&2; exit 1; }

[ -x "$BIN/pc3d" ] || die "no $BIN/pc3d"
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

for t in $TESTS; do
	if ! ( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$M/tests/$t.bas" ) > "$W/$t.build.log" 2>&1; then
		echo "FAIL  $t (build)"; tail -3 "$W/$t.build.log"; fail=1
		continue
	fi
	( cd "$W" && timeout 120 "$BIN/bcrun" "$W/$t.bc" < /dev/null > "$W/$t.raw" 2>&1 )
	echo "exit $?" >> "$W/$t.raw"
	# what the program left behind, then clear it for the next one
	( cd "$W" && ls *.bmp 2>/dev/null | sort | xargs -r sum | sed 's/^/bmp: /' ) >> "$W/$t.raw"
	rm -f "$W"/*.bmp
	# an `ls -l` line (saveimg.bas) carries a date and an owner: keep
	# the size and the name
	tr -d '\r' < "$W/$t.raw" | sed -E 's/^-[rwxsStT-]{9}[ ]+[0-9]+[ ]+[^ ]+[ ]+[^ ]+[ ]+([0-9]+)[ ]+[A-Za-z]+[ ]+[0-9]+[ ]+[0-9:]+[ ]+(.*)$/\1 \2/' > "$W/$t.out"
	if [ -n "$BLESS" ]; then
		cp "$W/$t.out" "$D/$t.expected" && echo "blessed $t"
	elif cmp -s "$W/$t.out" "$D/$t.expected"; then
		echo "pass  $t"
	else
		echo "FAIL  $t"
		[ -f "$D/$t.expected" ] && diff "$D/$t.expected" "$W/$t.out" | head -6
		fail=1
	fi
done
exit $fail
