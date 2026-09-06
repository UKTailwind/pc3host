#!/bin/bash
#
# Try the package without installing it: extract it somewhere, and from
# THERE - not /opt/pc3, so every path the programs find is found
# relative to themselves - build and run the examples against a
# headless server and compare with the end-to-end goldens.
#
#   bash packaging/test-deb.sh dist/pc3host_0.2.0_amd64.deb
#
# What this proves: the static binaries run; cc finds cpp, mmbc, its
# passes and headers, and bcrun, beside itself; bcrun finds saveimage
# beside itself; the launcher puts them on the PATH; the pictures and
# readbacks are the ones the build tree produces.

DEB=$1
R=$(cd "$(dirname "$0")/.." && pwd)
T=${T:-/tmp/pc3-debtest}
[ -f "$DEB" ] || { echo "usage: test-deb.sh <package.deb>" >&2; exit 2; }

rm -rf "$T"; mkdir -p "$T/root" "$T/work"
dpkg-deb -x "$DEB" "$T/root" || exit 1
P=$T/root/opt/pc3
fail=0

echo "--- contents"
dpkg-deb -I "$DEB" | sed -n '/Package:/,/Installed-Size:/p'
echo "bin: $(ls "$P/bin" | tr '\n' ' ')"
echo "lib/cc: $(ls "$P/lib/cc" | tr '\n' ' ') ($(ls "$P/lib/cc/include" | wc -l) headers)"
echo "share: $(ls "$P/share" | tr '\n' ' '); mmb2c: $(find "$P/share/mmb2c" -type f | wc -l) files"
for f in bcrun cc mmbc mmbedit; do
	file "$P/bin/$f" | grep -q "statically linked" && echo "$f: static" || { echo "$f: NOT static"; fail=1; }
done
# pc3d is dynamic on purpose (libX11's dlopen of libXcursor); what it
# may not do is need a glibc newer than 2.34 or a libstdc++ newer than
# Ubuntu 22.04's (GLIBCXX_3.4.30, GCC 12)
file "$P/bin/pc3d" | grep -q "dynamically linked" && echo "pc3d: dynamic" || { echo "pc3d: NOT dynamic"; fail=1; }
glibc_max=$(objdump -T "$P/bin/pc3d" | grep -o "GLIBC_[0-9.]*" | sort -t. -k2,2n -u | tail -1)
cxx_max=$(objdump -T "$P/bin/pc3d" | grep -o "GLIBCXX_[0-9.]*" | sort -t. -k3,3n -u | tail -1)
echo "pc3d needs $glibc_max ${cxx_max:-no GLIBCXX}; shared libraries: $(objdump -p "$P/bin/pc3d" | awk '/NEEDED/{printf "%s ", $2}')"
case $glibc_max in GLIBC_2.3[0-4]|GLIBC_2.[12]*) ;; *) echo "FAIL  pc3d needs $glibc_max (must be <= 2.34)"; fail=1;; esac
case ${cxx_max:-GLIBCXX_3.4} in GLIBCXX_3.4|GLIBCXX_3.4.[0-9]|GLIBCXX_3.4.[12][0-9]|GLIBCXX_3.4.30) ;; *) echo "FAIL  pc3d needs $cxx_max (must be <= 3.4.30)"; fail=1;; esac

export PC3_SOCKET=$T/pc3d.sock PC3_AUTOSTART=0
unset PC3_DISPLAY
"$P/bin/pc3d" --headless --socket "$PC3_SOCKET" &
SRV=$!
trap 'kill $SRV 2>/dev/null; wait $SRV 2>/dev/null' EXIT
for i in $(seq 1 50); do [ -S "$PC3_SOCKET" ] && break; sleep 0.1; done
[ -S "$PC3_SOCKET" ] || { echo "server did not start"; exit 1; }

check() {
	if cmp -s "$2" "$3"; then echo "pass  $1"; else echo "FAIL  $1"; diff "$3" "$2" | head -5; fail=1; fi
}

echo "--- cc -r gfx1.bas, through the launcher"
cp "$P/share/examples/gfx1.bas" "$T/work/"
( cd "$T/work" && "$P/bin/pc3" cc -r gfx1.bas ) > "$T/work/gfx1.log" 2>&1 || { echo "FAIL  cc -r gfx1.bas"; cat "$T/work/gfx1.log"; fail=1; }
check "gfx1 readback" "$T/work/gfx1.out" "$R/tests/e2e/gfx1.expected"
( cd "$T/work" && "$P/bin/saveimage" gfx1.bmp ) || { echo "FAIL  saveimage"; fail=1; }
check "gfx1 screen" "$T/work/gfx1.bmp" "$R/tests/e2e/gfx1.golden.bmp"

echo "--- cc -r gfxc.c: a C program against pico_ioctl.h from lib/cc/include"
cp "$P/share/examples/gfxc.c" "$T/work/"
( cd "$T/work" && "$P/bin/pc3" cc -r gfxc.c ) > "$T/work/gfxc.out" 2>&1 || { echo "FAIL  cc -r gfxc.c"; cat "$T/work/gfxc.out"; fail=1; }
check "gfxc output" "$T/work/gfxc.out" "$R/tests/e2e/gfxc.expected"

echo "--- the launcher as a shell"
out=$(echo 'command -v cc; command -v bcrun; cc 2>&1 | head -1' | "$P/bin/pc3" 2>&1)
echo "$out" | grep -q "^$P/bin/cc$" && echo "pass  pc3 shell puts cc first" || { echo "FAIL  pc3 shell: $out"; fail=1; }

echo "--- ./prog.bc by its #! line, with bcrun on the PATH"
( cd "$T/work" && "$P/bin/pc3" ./gfxc.bc ) > "$T/work/gfxc2.out" 2>&1 || { echo "FAIL  ./gfxc.bc"; fail=1; }
check "gfxc via #!" "$T/work/gfxc2.out" "$R/tests/e2e/gfxc.expected"

[ $fail = 0 ] && echo "test-deb: all passed" || echo "test-deb: FAILED"
exit $fail
