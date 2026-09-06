#!/bin/bash
#
# The translator corpus on Windows, by the rules of fcc/fcctests.sh:
# cc.exe builds each of mmb2c/tests/*.bas, the gate-shaped bcrun runs
# it, a tests/NAME.rc names an expected exit status and folds stderr
# into the output, and elapsed-time lines are filtered.
#
#   bash tests/win/corpus.sh <tree> <gatebin> <fuzix> <work>
#
# from Git Bash on Windows: <tree> holds bin/ and lib/cc/ (a build tree
# or an unpacked zip), <gatebin> the gate-shaped bcrun.exe (build-win/
# gatebin), <fuzix> the FUZIX tree the corpus lives in, <work> a
# directory of its own.  All 90 pass, with the same output the Linux
# gate compares against; KNOWN is here for a name that may one day
# have to differ, and is empty because none does.
T=$1; G=$2; FZ=$3; W=$4
KNOWN=" "
[ -d "$T/bin" ] && [ -x "$G/bcrun.exe" ] && [ -n "$W" ] || {
	echo "usage: corpus.sh <tree> <gatebin> <fuzix|mmb2c> <work>" >&2; exit 2; }
# <fuzix> may be a FUZIX tree or the mmb2c directory itself, which is
# what the package ships as share/mmb2c.
if [ -d "$FZ/Applications/mmb2c/tests" ]; then M=$FZ/Applications/mmb2c/tests
elif [ -d "$FZ/tests" ]; then M=$FZ/tests
else echo "corpus.sh: no test corpus under $FZ" >&2; exit 2; fi
rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
cp "$M"/*.bas "$M"/*.expected . 2>/dev/null
cp "$M"/*.in "$M"/*.rc . 2>/dev/null
pass=0; fail=0; ccfail=0; known=0
for b in *.bas; do
	n=${b%.bas}
	[ -f "$n.expected" ] || continue
	if ! timeout 120 "$T/bin/cc.exe" "$b" > "$n.cc" 2>&1; then
		echo "CCFAIL $n: $(tail -1 $n.cc)"; ccfail=$((ccfail+1)); continue
	fi
	exprc=0
	[ -f "$n.rc" ] && exprc=$(cat "$n.rc")
	if [ -f "$n.in" ]; then
		timeout 120 "$G/bcrun.exe" "$n.bc" < "$n.in" > "$n.out" 2> "$n.err"; rc=$?
	else
		timeout 120 "$G/bcrun.exe" "$n.bc" > "$n.out" 2> "$n.err" < /dev/null; rc=$?
	fi
	[ "$exprc" != 0 ] && cat "$n.err" >> "$n.out"
	if [ "$rc" != "$exprc" ]; then
		echo "FAIL $n (exit $rc, expected $exprc) $(tail -1 $n.err)"; fail=$((fail+1)); continue
	fi
	grep -v -e "Time taken" "$n.out" > "$n.out.f"
	grep -v -e "Time taken" "$n.expected" > "$n.exp.f"
	if cmp -s "$n.out.f" "$n.exp.f"; then
		pass=$((pass+1))
	elif [ "${KNOWN/ $n /}" != "$KNOWN" ]; then
		echo "known $n (differs from the Linux gate, see the head of this script)"; known=$((known+1))
	else
		echo "FAIL $n (output)"; diff "$n.exp.f" "$n.out.f" | head -4; fail=$((fail+1))
	fi
done
echo "corpus on Windows: pass $pass known $known fail $fail ccfail $ccfail"
[ $fail = 0 ] && [ $ccfail = 0 ]
