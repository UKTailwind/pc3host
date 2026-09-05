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

## Where things stand

Phase 0 built the tree and the gates and cut the first kernel seam.
Phase 1 is the display server: a BASIC program, or a C program written
against `pico_ioctl.h`, draws in a window through the kernel's own
display core, on Linux and under WSLg. See [PHASE0.md](PHASE0.md) and
[PHASE1.md](PHASE1.md). The keyboard, sound, the remaining programs,
Windows and networking are phases 2 to 7 in the review; a PicoMite on a
USB serial link for real I/O is phase 8.
