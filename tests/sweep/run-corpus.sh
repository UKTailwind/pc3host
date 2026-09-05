#!/bin/bash
#
# The board-shaped gate: every tests/*.bas with a .expected, run by the
# board-shaped bcrun against a headless display server, and compared with
# the expected output.  The .expected files were blessed display-less, so
# a difference is one of two things: a test that reads the screen back
# and now gets a real answer (the file's limitation), or a fault in the
# hosted chain (ours).  This prints the differences so they can be told
# apart; it does not decide.
#
#   bash tests/sweep/run-corpus.sh <bin dir> <FUZIX root> <work dir>

BIN=$1; FZ=$2; W=$3
M=$FZ/Applications/mmb2c
mkdir -p "$W"
export PC3_SOCKET=$W/corpus.sock PC3_AUTOSTART=0
unset PC3_DISPLAY
"$BIN/pc3d" --headless --socket "$PC3_SOCKET" > "$W/server.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; wait $SRV 2>/dev/null' EXIT
for i in $(seq 1 50); do [ -S "$PC3_SOCKET" ] && break; sleep 0.05; done

pass=0; diff=0; fail=0
for src in "$M"/tests/*.bas; do
	b=$(basename "$src" .bas)
	exp=$M/tests/$b.expected
	[ -f "$exp" ] || continue
	( cd "$W" && BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$src" ) > "$W/$b.build.log" 2>&1 \
		|| { echo "BUILD $b"; fail=$((fail+1)); continue; }
	exprc=0; [ -f "$M/tests/$b.rc" ] && exprc=$(cat "$M/tests/$b.rc")
	inp=/dev/null; [ -f "$M/tests/$b.in" ] && inp=$M/tests/$b.in
	( cd "$W" && timeout 60 "$BIN/bcrun" "$W/$b.bc" < "$inp" > "$W/$b.out" 2> "$W/$b.err" )
	rc=$?
	[ "$exprc" != 0 ] && cat "$W/$b.err" >> "$W/$b.out"
	grep -v 'Time taken' "$W/$b.out" | tr -d '\r' > "$W/$b.a"
	grep -v 'Time taken' "$exp" | tr -d '\r' > "$W/$b.b"
	if [ $rc != "$exprc" ]; then
		echo "EXIT  $b: $rc, expected $exprc: $(head -1 "$W/$b.err" | cut -c1-70)"; fail=$((fail+1))
	elif cmp -s "$W/$b.a" "$W/$b.b"; then
		pass=$((pass+1))
	else
		echo "DIFF  $b ($(diff "$W/$b.b" "$W/$b.a" | grep -c '^[<>]') lines)"; diff=$((diff+1))
	fi
done
echo "corpus with a display: $pass identical, $diff differ, $fail failed"
