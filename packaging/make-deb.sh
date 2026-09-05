#!/bin/bash
#
# Build the Debian package: dist/pc3host_<version>_<arch>.deb
#
#   bash packaging/make-deb.sh                # from the submodule's FUZIX
#   FUZIX_ROOT=/home/me/src/FUZIX bash packaging/make-deb.sh
#
# Everything is linked statically (PC3_STATIC), so the package installs
# on any Linux of the same architecture whatever its C library, and
# needs nothing but an X server to show a window.  Needs cmake, gcc,
# g++, the X11 development files (libx11-dev, libxcb1-dev, libxau-dev,
# libxdmcp-dev), dpkg-deb and fakeroot.
#
# Layout: /opt/pc3/{bin,lib/cc,share} as CMakeLists.txt installs it, and
# /usr/bin symlinks for the names that clash with nothing on a PC.  cc
# and cpp are NOT linked into /usr/bin: `pc3` puts /opt/pc3/bin first on
# the PATH instead, and pc3cc is the direct name.

set -e
R=$(cd "$(dirname "$0")/.." && pwd)
VER=$(tr -d '[:space:]' < "$R/VERSION")
ARCH=$(dpkg --print-architecture)
B=$R/build-deb
S=$B/stage
OUT=$R/dist/pc3host_${VER}_${ARCH}.deb

for t in cmake gcc g++ dpkg-deb fakeroot; do
	command -v $t > /dev/null 2>&1 || { echo "make-deb: no $t" >&2; exit 1; }
done

rm -rf "$S"
cmake -S "$R" -B "$B" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/pc3 \
      -DPC3_STATIC=ON ${FUZIX_ROOT:+-DFUZIX_ROOT=$FUZIX_ROOT} > "$B.configure.log" 2>&1 \
	|| { cat "$B.configure.log"; exit 1; }
cmake --build "$B" -j"$(nproc)" 2>&1 | grep -E "error|warning: .*(pc3|keyboard|dispatch)|FAILED" || true
DESTDIR=$S cmake --install "$B" > /dev/null

# the names a PC has no other use for
mkdir -p "$S/usr/bin" "$S/DEBIAN"
for f in pc3 pc3d bcrun bcdump mmbc mmedit pc3key saveimage loadimage loadjpg loadpng \
         playmp3 playwav playflac playmod playsnd; do
	ln -sf /opt/pc3/bin/$f "$S/usr/bin/$f"
done
ln -sf /opt/pc3/bin/cc "$S/usr/bin/pc3cc"

SIZE=$(du -sk --exclude=DEBIAN "$S" | cut -f1)
sed "s/@VERSION@/$VER/; s/@ARCH@/$ARCH/; s/@SIZE@/$SIZE/" "$R/packaging/control.in" > "$S/DEBIAN/control"
( cd "$S" && find . -type f ! -path './DEBIAN/*' -exec md5sum {} \; | sed 's| \./| |' ) > "$S/DEBIAN/md5sums"

mkdir -p "$R/dist"
fakeroot dpkg-deb --build "$S" "$OUT" > /dev/null
echo "built $OUT"
ls -la "$OUT"
