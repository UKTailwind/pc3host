# pc3host

The Pico Computer 3's BASIC environment, hosted on a PC: the `mmbc`
translator, the Compiler Kit `cc`, the `bcrun` runtime, the image
loaders, the players and `mmedit`, running natively on Linux, WSL and
Windows with MiniFB for the display and miniaudio for sound.

Start with [REVIEW.md](REVIEW.md), which surveys what exists in the
FUZIX `pc3` tree, proposes the architecture (a device server that plays
the kernel's part, plus a client library behind the runtime's
`/dev/sys` calls), weighs `cc` against a native compiler, and lays out
the phases. [PHASE0.md](PHASE0.md) records what phase 0 built and
proved.

## Building

```
git submodule update --init
cmake -S . -B build
cmake --build build -j8
ctest --test-dir build
```

Every tool is compiled from where it lives in the FUZIX tree, which is
the submodule at `ext/FUZIX`; nothing is copied here. Pass
`-DFUZIX_ROOT=<path>` to build against a live FUZIX checkout instead.

## Running a program

```
build/bin/pc3d &                              # the display, in a window
build/bin/mmbc prog.bas -o prog.c             # translate
# compile with cc to prog.bc (tests/e2e/run.sh shows the fccbuild.sh call)
build/bin/bcrun prog.bc                       # draws in the window
build/bin/saveimage screen.bmp                # the screen, from another process
```

The server starts itself when a program first opens the display, so the
first line is optional. `PC3_DISPLAY=off` runs a program with no display.

## Installing

`bash packaging/make-deb.sh` builds `dist/pc3host_<version>_amd64.deb`,
statically linked so it installs on any Linux of the architecture it
was built on. It puts the tree under `/opt/pc3` and links the names a
PC has no other use for into `/usr/bin`. The PC3's compiler is called
`cc`, as on the board, and is deliberately not linked there: `pc3`
opens a shell with `/opt/pc3/bin` first on the PATH, or runs one
command that way.

```
sudo dpkg -i dist/pc3host_0.2.0_amd64.deb
pc3                                   # a shell with the PC3 tools first
cc -r /opt/pc3/share/examples/gfx1.bas
```

## Where things stand

Phase 0 built the tree and the gates and cut the first kernel seam.
Phase 1 is the display server: a BASIC program, or a C program written
against `pico_ioctl.h`, draws in a window through the kernel's own
display core, on Linux and under WSLg. Phase 2 is the keyboard: the
window's keys reach `INKEY$` and `KEYDOWN` through the PC3's own
decoder, merged with the terminal's. Phase 3 is sound: the kernel's own
synths and PCM ring, compiled into the server and played through
miniaudio, so `PLAY SOUND`, `PLAY TONE` and the WAV, MP3, FLAC and MOD
players work as they do on the board. Phase 4 is the programs around
a program: the image loaders and `saveimage` (now reading rows, not
pixels), sprites down their pipe, `SYSTEM`, gated by the image round
trips. Phase 5 is the editor and the driver: `mmedit prog.bas` then F2
builds and runs, as the manual says, through a compiler you can name.
Phase 7 is networking: the `WEB` family over the PC's sockets, TLS
through mbedtls in `bcrun` itself, names resolved, `WEB CONNECT`
answered from the machine's own network. See [PHASE0.md](PHASE0.md),
[PHASE1.md](PHASE1.md), [PHASE2.md](PHASE2.md), [PHASE3.md](PHASE3.md),
[PHASE4.md](PHASE4.md), [PHASE5.md](PHASE5.md) and
[PHASE7.md](PHASE7.md). Windows is phase 6 in the review, deferred
until everything works under Linux; a PicoMite on a USB serial link for
real I/O is phase 8.
