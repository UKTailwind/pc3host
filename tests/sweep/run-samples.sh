#!/bin/bash
#
# Sweep: every sample (and anything else named) through cc and bcrun
# against a fresh headless server each, with a time limit; record how it
# ended, what it said, and what it left on the screen.
#
#   bash tests/sweep/run-samples.sh <bin dir> <work dir> <file.bas>...
#
# Per program, in <work dir>:  NAME.log (cc), NAME.out (stdout),
# NAME.err (stderr), NAME.rc (exit status, 124 = still running at the
# limit), NAME.bmp (the screen afterwards).  A summary table at the end.
# Programs that read the keyboard get no keys; programs that read stdin
# get EOF; that is what the sweep is for - to see what happens then.
#
# Each program runs with a COPY of its directory as the working
# directory (<work dir>/src/<dirname>), so the relative files it opens -
# tile sheets, MOD files - resolve, and the files it writes - SAVE
# IMAGE, a rewritten copy of itself - land in the work directory and
# not in the source tree.  The first sweep left six of those in
# samples/.

BIN=$1; W=$2; shift 2
LIMIT=${LIMIT:-4}
mkdir -p "$W"
export PC3_SOCKET=$W/sweep.sock PC3_AUTOSTART=0
unset PC3_DISPLAY

start_server() {
	"$BIN/pc3d" --headless --socket "$PC3_SOCKET" > "$W/server.log" 2>&1 &
	SRV=$!
	for i in $(seq 1 50); do [ -S "$PC3_SOCKET" ] && break; sleep 0.05; done
}
stop_server() {
	kill $SRV 2>/dev/null; wait $SRV 2>/dev/null
}
trap 'stop_server' EXIT

printf '%-14s %4s  %s\n' program rc "first line of stderr / note" > "$W/summary.txt"
for src in "$@"; do
	b=$(basename "$src" .bas)
	dir=$(cd "$(dirname "$src")" && pwd)
	run=$W/src/$(basename "$dir")
	[ -d "$run" ] || { mkdir -p "$W/src" && cp -r "$dir" "$run"; }
	start_server
	( cd "$run" && "$BIN/cc" -o "$W/$b.bc" "$run/$b.bas" ) > "$W/$b.log" 2>&1
	if [ ! -s "$W/$b.bc" ]; then
		printf '%-14s %4s  %s\n' "$b" "CC" "$(grep -v '^wrote\|^$' "$W/$b.log" | head -1 | cut -c1-70)" >> "$W/summary.txt"
		stop_server; continue
	fi
	inp=/dev/null
	[ -f "$run/$b.in" ] && inp=$run/$b.in
	( cd "$run" && timeout $LIMIT "$BIN/bcrun" "$W/$b.bc" < "$inp" > "$W/$b.out" 2> "$W/$b.err" )
	rc=$?
	echo $rc > "$W/$b.rc"
	"$BIN/saveimage" "$W/$b.bmp" > /dev/null 2>&1
	note=$(grep -v '^$' "$W/$b.err" | head -1 | tr -d '\r' | cut -c1-70)
	[ -z "$note" ] && [ $rc = 124 ] && note="(running at the limit)"
	[ -z "$note" ] && [ $rc = 0 ] && note="ok, $(wc -l < "$W/$b.out") lines out"
	printf '%-14s %4s  %s\n' "$b" "$rc" "$note" >> "$W/summary.txt"
	stop_server
done
cat "$W/summary.txt"
