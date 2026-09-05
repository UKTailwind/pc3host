#!/bin/bash
#
# The sound seam gate: sound_seam drives the kernel's sound core against
# stub hooks and checks its rules; then its MMBasic synth output is
# compared with sndharness's, which is playsnd's renderer standing on
# its own.  The kernel synth is that renderer moved into the interrupt,
# so the two must agree sample for sample once the synth's volume ramp
# has finished (41 steps of 44 frames; sndharness starts at full
# volume).  From frame 2048 to the end of the 5 seconds, byte for byte.
#
#   bash tests/seam/sound_seam.sh <bin dir> <work dir>

BIN=$1; W=$2
mkdir -p "$W" || exit 1
fail=0

"$BIN/sndharness" "$W/ref.wav" 440 > /dev/null || { echo "FAIL  sndharness"; exit 1; }
"$BIN/sound_seam" "$W" || fail=1

SKIP=$((44 + 2048 * 4))
LEN=$(( (220500 - 2048) * 4 ))
a=$(tail -c +$((SKIP + 1)) "$W/ref.wav" | head -c $LEN | md5sum | cut -d' ' -f1)
b=$(tail -c +$((SKIP + 1)) "$W/mm.wav" | head -c $LEN | md5sum | cut -d' ' -f1)
if [ "$a" = "$b" ]; then
	echo "pass  the kernel synth is sndharness, sample for sample ($LEN bytes)"
else
	echo "FAIL  the kernel synth differs from sndharness"
	cmp <(tail -c +$((SKIP + 1)) "$W/ref.wav" | head -c $LEN) \
	    <(tail -c +$((SKIP + 1)) "$W/mm.wav" | head -c $LEN) | head -2
	fail=1
fi
exit $fail
