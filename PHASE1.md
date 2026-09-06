# Phase 1: the display server on Linux

What REVIEW.md section 6 asked of phase 1, and where it stands. Dated
2026-09-05.

## What works

A BASIC program translated by `mmbc`, compiled by `cc` and run by
`bcrun` draws on a PC. So does a C program written for the PC3 against
`pico_ioctl.h` and compiled by the same `cc`. The picture appears in a
window sized to the raster the PC3 would be scanning out, drawn by the
kernel's own display core, and `saveimage` takes it from a separate
process afterwards exactly as on the board, because the picture lives
in the server and not in the program.

```
pc3d &                          # the display server; a window appears
mmbc prog.bas -o prog.c         # or cc -r prog.bas once phase 5 wires it
bcrun prog.bc                   # draws in the window
saveimage screen.bmp            # the screen, from another process
```

A program does not have to start the server: the first open of the
display starts `pc3d` if none is listening, found beside the program or
on the PATH, and detached so it outlives the program. `PC3_DISPLAY=off`
makes the display absent, which is how the gates keep running
display-less, and `PC3_SOCKET`, `PC3_AUTOSTART` and `PC3D` are the
environment (`proto/pc3proto.h`).

The window shows the 640×480 raster at twice its size, 1280×960, so a
MODE 2 pixel, which is already 2×2 in that raster, is four window
pixels across, and the console's 8×12 cells are 16×24. `--scale N`
(1 to 4) changes it. The 1024×768 raster of the BBC modes is shown one
to one; doubling it would want a 2048×1536 window. The scale is applied
after the expander has done its work, so every window pixel is still
one the PC3's scanout would have made, only repeated.

`pc3d --snapshot FILE.ppm` writes the window's pixels, scale and all,
each time a client disconnects: a way for a script, or a person with no
window, to see what the window showed.

## The pieces

| piece | where | what |
|---|---|---|
| wire protocol | `proto/pc3proto.h` | framing plus flattened forms of the six pointer-bearing structures; everything else is `pico_ioctl.h`'s own bytes |
| client library | `client/pc3client.c` | `pc3_sys_open`, `pc3_sys_ioctl`, `pc3_sys_close`; marshalling per code; the PSRAM arena, `PICOIOC_RANDOM` and `PICOIOC_LIBM` answered locally |
| server | `server/pc3d.c` | one thread: `poll()` over the socket and every client, a frame tick at the raster's rate (59.5 Hz VGA, 70.1 Hz XGA), `VSYNC` and `VSYNCTRY` answered from the tick, per-connection state released on disconnect |
| dispatch | `server/dispatch.c` | `misc.c`'s ioctl switch for the 28 graphics codes, `INFO`, `CONMIRROR`, `KEYDOWN` (zeros until phase 2) and `BOARD`; same limits, same errno |
| hardware half | `server/disphw.c` | the `display_priv.h` hooks: framebuffers as arrays, a raster note, no barrier, no wait |
| presenter | `server/present.c` | the five scanline expanders, including MODE 0's 5:8 coverage blend copied from the kernel's LUT builder |
| window | `server/window.c` | MiniFB, X11 without OpenGL; a raster change closes and reopens the window |
| kernel code compiled in | `display.c`, `fonts.c` | unchanged, from the FUZIX tree; `fonts.c` sees `<kernel.h>` and `<kdata.h>` through `server/kstub/` |

## The seam in the FUZIX tree

Every program reached the hardware through `open("/dev/sys")` and
`ioctl()`. Those two calls now have one door each, and on the board the
doors are the calls they replaced:

* `mmb_runtime.c`: `mm_sys_open()` and `mm_sys_ioctl()` wrap the five
  opens and forty-eight ioctls on the system device. `/dev/gpio` is
  untouched; the pin path has no host.
* `bcrun.c`: a bytecode program's `open` of `/dev/sys`, and `ioctl` and
  `close` on the descriptor it gets, go to the client library under
  `PC3_HOST`. The pointer-bearing structures a 32-bit program hands over
  are rebuilt for the 64-bit host in `host_sys_ioctl`.
* `utils/pc3sys.h`: the same door for the image programs (`loadjpg`,
  `loadpng`, `loadimage`, `saveimage`, the PNG arena client).

`PC3_HOST` is the one define, shared with `display_priv.h`. The pc3host
`bcrun` is built with it plus `MM_PC3` and `MM_HOSTED_ONLY`: the first
is the board's own define and selects the runtime's real hardware paths
(the gate build in `Makefile.host` leaves it off and gets the
display-less runtime); the second says there is no kernel behind them,
so the maths table keeps the local functions instead of asking for the
kernel's.

## What the host found in the runtime

Three things the board could never show, all fixed in the FUZIX tree:

1. **The maths table under `MM_HOSTED_ONLY` was empty.** `MM_PC3`
   empties it for the kernel to fill and `MM_HOSTED_ONLY` skipped the
   fill, so the first `SIN` would have called a null pointer. The guard
   on the table now matches the guard on the fill.
2. **`mm_us_fast` read TIMER0.** The pixel queue's age bound was a load
   from `0x400B0028`, which on a PC is a fault. Under `PC3_HOST` it is
   the monotonic clock. This was the segmentation fault the first run
   produced.
3. **Seven mirror structures and two colour arrays used `long`.** They
   copy kernel structures whose fields are 32 bits; `long` is 32 on the
   board and 64 on a PC, so the colours handed to a batch were read at
   the wrong stride (every second pixel black) and the text run's
   fields were in the wrong places (no text). They are `int` now, which
   is the type they always were on the board. The ARM cross-compile of
   `bcrun.o` and the image programs is unchanged in size.

And one in the translation layer: a 32-bit `struct gfx_text` puts its
pointer at offset 20, not 16, because `fg` is aligned to 4. The C test
found it.

## Two bcruns

The board's bcrun is built with `MM_PC3`, and with no display attached
it refuses what the board would refuse: `BLIT`, `SPRITE`, `FRAMEBUFFER`,
the pins. The gate build in `Makefile.host` leaves `MM_PC3` off and
gets the display-less runtime, which says "not here" and carries on,
and that is the shape every `tests/*.expected` was blessed against. The
first full run of the suite with a board-shaped `bcrun` failed eighteen
of ninety programs for exactly that reason. So the tree builds both:
`build/bin/bcrun` is the board shape for running programs, and
`build/gatebin/bcrun` is the gate shape, beside links to the passes, and
the fcc gates point there. A board-shaped gate, with expected outputs
blessed against a headless server, is the natural next step and would
be a stronger check than either.

## Gates

`ctest` now runs seven tests. The five from phase 0 are unchanged in
what they prove, with `PC3_DISPLAY=off` in the fcc gates' environment
and the gate-shaped bcrun. Two are new:

| test | what it proves |
|---|---|
| `seam-display` | (phase 0) the display core without the kernel |
| `e2e-display` | `tests/e2e/run.sh`: a headless `pc3d`; `gfx1.bas` in MODE 2 through `mmbc`, `cc`, `bcrun` and the runtime, its `PIXEL()` readbacks and its screen against goldens; `gfxc.c` in MODE 0 through `cc` and bcrun's libcalls, its `GETPIXEL` and `INFO` output and its screen against goldens |

The goldens were made by this run and looked at before they were kept
(`tests/e2e/*.golden.bmp`; `bmp2png.py` turns a screen into something a
viewer opens). The readbacks were checked by hand against the RGB121
palette: magenta is 16711935, the box fill is 255, the batch reads
back red, green, blue and white in order.

Also seen, not automated: `pc3d` with a window under WSLg, opening a
640x480 X11 window and running at sixty frames a second while the
BASIC program drew into it.

## Not in phase 1, deliberately

* **The console in the window.** `console_gfx()` is a stub and
  `CONMIRROR` only records. A BASIC program's `PRINT` in a graphics
  mode already reaches the screen, because the runtime draws it with
  `GFXIOC_TEXT`; what does not is the shell's own text. The terminal is
  the text console for now, as the review's two-console mapping says.
* **Keyboard** (phase 2): `KEYDOWN` answers zeros; `INKEY$` is the
  terminal.
* **Sound** (phase 3): the players still open `/dev/sys` directly and
  fail politely; the sound codes are `ENOTTY`.
* **`SAVE IMAGE` and `LOAD IMAGE` from BASIC** spawn `/usr/bin/…` by
  absolute path and so do not find the host binaries yet (phase 5).
* **`PICOIOC_BOARD` answers 3.** A PC is not a PC3, but a program
  asking `MM.DEVICE$` wants the machine it was written for. Worth a
  decision when the console phase gives the host a name of its own.
* **`GFXIOC_FONTADDR`** hands back a copy of the font mapped below 4G
  so the 32-bit address fits; Linux only (`MAP_32BIT`), like the arena.

## The presenter thread (added 2026-09-06)

The first user found `breakout.bas` crawling on a desktop where it ran
at 110 frames a second here, and the difference was the window. The
server presented from its request loop: expand the framebuffer, push it
to X, then answer requests again, and while the push was in flight
nobody was answered. Under WSLg a push takes two milliseconds and it
did not show. On a desktop whose compositor holds a client until it has
taken the previous frame, a push can be most of a frame, and a program
making 1,200 round trips a frame - `TILEMAP DRAW` is one per tile - was
served in the gaps between pushes. The board never has this problem:
core1 scans out whatever core0 does, and no ioctl waits for a frame.

`server/presenter.c` is that core1. The window, MiniFB and the X11
connection live on its thread. The request loop holds a display lock
around every dispatch; the presenter takes the same lock only while it
turns the framebuffer into window pixels, a millisecond or two, and
pushes without it. A tick wakes the presenter, and it presents when
something has been drawn since the last frame - a static screen costs
nothing; the keys fixture ran 274 frames and presented twice. If a push
outlasts a frame the frames coalesce, as they do on a board whose
program draws faster than the monitor shows. The window's key events
are queued in `keyboard.c` under a lock and taken by the request loop,
which the presenter wakes through a pipe so a key is never a frame
late; a window closed from the desktop stops the server the same way.
`--verbose` now reports how many frames were presented and what each
cost. Typing into the real window with XTEST gives the e2e keys
fixture's exact expected output through the new path.

A closer look at the crossing count: the tilemap header already draws
through a program-side window of six rows a transfer, so a full-screen
`TILEMAP DRAW` is about 80 round trips, not 1,200, and most of its 6
ms here is `bcrun` interpreting the tile composition. The game runs at
8 ms a frame on this machine, headless or windowed, with the presenter
or without; whatever made it crawl on the first user's desktop was not
reproduced here, and the version handshake below is the first suspect.

**The blanking wait was not waiting (found 2026-09-06).** The runtime
waits for the top of blanking by asking `GFXIOC_VSYNCTRY` for up to 2
ms at a time, 32 tries, and the kernel spends each budget waiting. The
server answered 0 *at once* when the frame was further off than the
budget, so the 32 tries were over in a millisecond and `FRAMEBUFFER
COPY ,B` and `FRAMEBUFFER MERGE` never waited for anything: a copy with
`,B` cost 1.3 ms where the board's costs a frame, and a game paced that
way would have run far too fast. The server now keeps a deadline for
the budget and answers 1 at the frame tick if it falls inside it, else
0 when the budget has elapsed. `pc3bench.bas` shows the copy at 16.80
ms, one frame.

**Fewer crossings, a faster interpreter (2026-09-06).** The first
user's report that the game became playable only with its ball speed
raised from 0.4 to 3 says their frame took about seven times the
board's, where this machine's takes a fraction of it. Two things a
desktop does differently from this VM scale exactly with a frame: what
a round trip costs (a socket ping-pong between two processes can cost
hundreds of microseconds on a desktop whose cores sleep deeply between
requests) and how fast `bcrun` interprets. So two levers, both host
only: the driver builds a hosted program with a 16K row window
(`-DMMB_WINB=16384`; the board keeps 1K, where a program has 48K and an
ioctl costs a microsecond), so a full 320x240 screen is three
transfers rather than forty and the game's tilemap draw goes from 80
crossings a frame to 6; and `bcrun` is built `-O2` rather than the
kit's `-O1`. Here the loop went from 7.5 to 4.8 ms a frame. On a
machine where a crossing is dear the saving is the larger part.

**Closing the window (found 2026-09-06).** The first user found that
after a reboot everything ran, and that closing the window with its
button left something behind: the next run found "a server already
listening" and no window. Two things were wrong. The server's exit
path shut the audio device down *before* closing its socket, so for
as long as that shutdown took - on a desktop where it stalls, for good
- a live socket with no window sat there, and only a reboot cleared it;
the socket is closed and unlinked first now, and the audio shutdown
gets two seconds. And the program whose window was closed ran on
unseen: one waiting in `INKEY$` spun with nothing to read, one drawing
drew into the void. On the board the display never goes away; on a PC
the closed window is the user stopping the program, so the client now
interrupts the program when its server goes - as the window's Ctrl-C
does, or with SIGTERM where a background job has SIGINT ignored.
Pressed through the X server with `tools/xclose.c`: with a program at
its title screen the server and the program both exit; with none, the
server exits; the next run starts a fresh server. A server started by a
program also writes its messages to `pc3d.log` beside its socket now,
rather than to the program's stdout, which in a pipeline it held open
for as long as it lived. And the window's title follows the MODE, not
the raster: MMBasic's MODE 2 shares the console's and kept its name.

**A server outlives an upgrade.** The display server stays up after a
program exits, as the board's picture does, so it survives a package
upgrade and a new program talks to the old server without knowing.
The client now asks the server its version once per process
(`PC3_VERSION_REQ`) and says on stderr when the answer is not its own
version, or when the server is too old to know the question; the
package's `postinst` stops any running `pc3d` so the next program
starts the new one. `tests/bench/pc3bench.bas` (installed under
`share/examples`) reports what each kind of statement costs on the
machine it runs on, and `samples/breakout_timed.bas` is the game with
a stopwatch on each part of its loop - the two things to run on a
machine where a program is slow.

## Building

```
git submodule update --init
cmake -S . -B build
cmake --build build -j8
ctest --test-dir build
```

MiniFB is a submodule at `ext/minifb`, built without OpenGL; the server
links as C++ because MiniFB carries a C++ file. Needs the X11
development headers.
