# pc3host

The Pico Computer 3's BASIC environment, hosted on a PC: the `mmbc`
translator, the Compiler Kit `cc`, the `bcrun` runtime, the image
loaders, the players and `mmbedit`, running natively on Linux, WSL and
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
sudo dpkg -i dist/pc3host_0.8.0_amd64.deb
pc3                                   # a shell with the PC3 tools first
cc -r /opt/pc3/share/examples/gfx1.bas
```

## Windows

The same tree cross-builds for Windows with MinGW-w64, so one Linux
machine makes both packages:

```
apt install mingw-w64
bash packaging/make-zip.sh            # dist/pc3host-<version>-win64.zip
```

Unpack the zip anywhere and run `pc3.cmd`: it opens a command prompt
with the tools on the PATH. Nothing is installed and no runtime is
needed - every program is linked statically. `selftest.cmd` compiles
the eclipse predictor with the shipped compiler and checks its output
to the last digit. The gates are in `tests/win` and travel with the
package under `share\tests`. [PHASE6.md](PHASE6.md) is the account of
what the platform made us change, and the shim itself is
`hostshim/win32`.

## Editing the examples

The examples and the MMBasic directory install under `/opt/pc3/share`
and belong to root, so `mmbedit` can read them there but not save them
- it says so on its status line when it opens one. Copy them somewhere
of your own first:

    cp -r /opt/pc3/share/examples ~/pc3
    cd ~/pc3/samples && mmbedit breakout.bas

## The window

The first program to draw starts the display server and opens the
window; the server stays up after the program ends, so the picture
survives it, as on the board. Closing the window with its button stops
the server and interrupts whatever program is using it, as Ctrl-C
would. A server started by a program writes its messages to
`pc3d.log` beside its socket (`$XDG_RUNTIME_DIR`, usually
`/run/user/<uid>`).

## If a program is slow

The display server stays up after a program exits, so it survives a
package upgrade too: after installing a new version, stop the old
server (`pkill pc3d`) or log out and in, or the next program will
still be talking to it. A program now says so if that happens.

Every graphics statement on a PC is a round trip to the server; on the
board it is a microsecond ioctl. `pc3bench.bas` reports what each kind
of thing costs on the machine it runs on, with a reference column from
a fast one:

    pc3cc -r /opt/pc3/share/examples/pc3bench.bas

and `samples/breakout_timed.bas` is the breakout game with a stopwatch
on each part of its loop. `pc3d --verbose` prints, on exit, how many
frames it presented and what each cost. Those three outputs say where
the time goes.

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
trips. Phase 5 is the editor and the driver: `mmbedit prog.bas` then F2
builds and runs, as the manual says, through a compiler you can name.
Phase 7 is networking: the `WEB` family over the PC's sockets, TLS
through mbedtls in `bcrun` itself, names resolved, `WEB CONNECT`
answered from the machine's own network. Phase 6 is Windows: the same
tree built with MinGW over a POSIX shim, 26 native programs in a zip
that needs nothing installed, and every gate green there too. See
[PHASE0.md](PHASE0.md), [PHASE1.md](PHASE1.md), [PHASE2.md](PHASE2.md),
[PHASE3.md](PHASE3.md), [PHASE4.md](PHASE4.md), [PHASE5.md](PHASE5.md),
[PHASE6.md](PHASE6.md) and [PHASE7.md](PHASE7.md). A PicoMite on a USB
serial link for real I/O is phase 8, and the only one left.
