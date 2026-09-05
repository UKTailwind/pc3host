# Hosting the PC3 BASIC environment on Linux, WSL and Windows

A review of what it would take to run the Fuzix-side MMBasic toolchain
(mmbc, the Compiler Kit `cc`, `bcrun`, the image loaders, the players and
`mmedit`) as native programs on a PC, with MiniFB for the display,
miniaudio for sound, and the host keyboard behind `INKEY$` and `KEYDOWN`.

Written 2026-09-05 against FUZIX `pc3` at `e48007c85` and the
MicroPython `pico-computer-3` branch at `baaf5e31d`. Every line count
and measurement below was taken from those trees on that day; the
board numbers are the ones recorded in the trees' own notes.

---

## 1. Summary

**It is feasible, and most of it already runs on a PC.** The translator
is host-built C, the compiler passes and `bcrun` have a host build that
the test gates use daily, `mmedit` compiles with plain gcc, and the
runtime compiles display-less for the gates. The decoders (picojpeg,
upng, dr_mp3, dr_wav, dr_flac, hxcmod) are portable by construction,
and the keyboard decoder already runs on a PC inside the MicroPython
emulator.

**What is missing is the kernel.** On the PC3 every piece of hardware
is reached through one device, `/dev/sys`, and 77 numbered `ioctl`
codes dispatched from `misc.c`. The kernel owns the framebuffers, the
audio DMA, the USB keyboard, and the per-process bookkeeping that
releases all of it when a process dies. None of that exists on Linux
or Windows, so the port is, at heart, **a process that plays the
kernel's part**: it owns a MiniFB window, a miniaudio device and the
keyboard state, and it answers those 77 requests over a local socket.
Everything else links a thin client library instead of calling
`ioctl`.

**Recommended shape.** A device server plus a client library, keeping
the multi-process design exactly as it is (players and loaders as
separate programs, `SYSTEM` as fork-and-exec). The existing two-console
design maps onto the PC directly: the terminal is the serial console
and the window is the screen.

**Compiler.** Ship `cc` to bytecode under `bcrun` as the primary
engine. It is what the board runs, it needs no toolchain on the user's
PC, it is the same on Windows and Linux, and the host interpreter is
already about twenty times faster than the board's native code. Offer
gcc or clang as an optional accelerator and as the developer's
debugging path. Section 5 has the argument in full.

**Size.** Roughly 4,000 lines of new code (server, client library,
platform shims) and 3,000 lines of kernel code ported behind seams,
in seven phases (section 6). Nothing in the runtime, the translator or
the compiler needs to change in a way the board would notice.

---

## 2. What exists today

| component | where (FUZIX unless noted) | lines | builds on a PC today | talks to hardware through |
|---|---|---|---|---|
| `mmbc`, the translator in C | `Applications/mmb2c/mmbc` | 15,269 | yes, gcc | nothing |
| `mmb2c.py`, the reference translator | `Applications/mmb2c` | 10,912 | yes, CPython | nothing |
| runtime `mmb_runtime.c` / `.h` | `Applications/mmb2c` | 6,265 + 905 | yes, display-less (`make check`) | `/dev/sys` ioctls, termios, fork/exec, FIFO |
| feature headers `mmb_*.h` | `Applications/mmb2c` | ~27,000 | yes | runtime libcalls only (the `libgate.sh` rule) |
| `cc0`, `cc1`, `cc2`, `cpp` | `Applications/CC`, `Applications/cpp` | cc1 6,283; cpp 2,873 | yes, `Makefile.host` | files |
| `bcrun` + `bcrun_mm.c` | `Applications/CC` | 3,922 + 815 | yes, `host-armm0/bcrun`; also `qemu-armm0` | `mmap` at a fixed address; native Thumb on ARM only |
| `ccbc`, the `cc` driver | `Applications/CC/ccbc.c` | small | yes | fork, pipes, `/usr/lib/cc` paths |
| `mmedit` | `Applications/mmedit` | 4,486 | yes, plain gcc with a pty harness | termios, two mode ioctls, `execv /usr/bin/cc` |
| `playmp3`, `playwav`, `playflac`, `playmod`, `playsnd` | `Kernel/platform/platform-rpipico/utils` | 143 / 108 / 147 / 287 / 464 + `pcmplay.h` | decoders yes | `SNDIOC_PCM*`, the control FIFO, `SIGINT` |
| `loadjpg`, `loadpng`, `loadimage`, `saveimage` | same | 362 / 358 / 676 / 156 | decoders yes | `GFXIOC_PIXELS`, `GETPIXEL`, PSRAM arena, pipes |
| display core `display.c` | kernel | 2,202 (about 1,100 drawing and framebuffer, 700 scanout) | no | HSTX, DMA, core1 |
| console engine `console.c` | kernel | 990 | no, but the CSI engine is plain C | the 1bpp framebuffer, the uart |
| sound `sound.c` | kernel | 1,183 | no, but the synths and the ring are plain C | PIO I2S, DMA |
| keyboard decoder `kbd_decode.c` | kernel and `ports/rp2` | 534 | **yes, in the MicroPython emulator** | HID boot reports in |
| fonts | kernel `fonts/` | 176 KB of headers | data | none |
| ioctl dispatch `misc.c` | kernel | 1,227 | no; this is what the server replaces | everything |

Two things in the MicroPython tree are direct prior art:
`boards/PICO_COMPUTER_3/emulator/src` holds `hdmi_sdl.c` (340 lines),
`kbd_sdl.c` (375), `i2s_sdl.c` (292), `pins_sdl.c` and `mouse_sdl.c`,
which are exactly the three seams this port needs, done with SDL2 for
the MicroPython firmware. Its `DESIGN.md` also records the decision not
to emulate the silicon, which holds here for the same reasons.

The gates that already exist and would carry straight over: `make
check` (gcc, every `tests/*.bas` against `.expected`), `fcc/fcctests.sh`
(the same programs through `cc` and `bcrun`), `hosttest/ctest.sh` (165
of 175 on the C89 conformance subset), `mmbc/cgate.sh` (byte-identical
C from the Python and the C translators), `libgate.sh`, and the
pixel-exact `blitharness`.

---

## 3. The seam: one device, 77 codes

Everything a program asks of the hardware goes through
`open("/dev/sys")` followed by `ioctl(fd, code, arg)`. `pico_ioctl.h`
declares the codes in **one flat number space**, and `ioctlcheck.sh`
fails on a duplicate. By family:

| family | codes | what | portable? |
|---|---|---|---|
| `GFXIOC_` | 28 | mode, colour, pixel, rect, batches, blit (4 forms), bitmap, text, fonts (3), palette (`MAP`, `MAPCTL`, `PAL`), scroll (2), framebuffers (`FBOPEN`, `FBSEL`, `FBCOPY2`, `MERGE`), vsync (2), info | yes, in the server |
| `SNDIOC_` | 11 | BBC `SOUND`/`ENVELOPE`/`QUIET`, PCM stream (open, write, stat, wait, close, owner), MMBasic synth (`MMCMD`, `MMSTOP`) | yes, in the server |
| `PICOIOC_` | 17 | `KEYDOWN`, `KBDMAP`, `NUMLOCK`, `CONMIRROR`, `RANDOM`, `BOARD`, `ADVAL`, `LIBM`, `RTCREG`, I2C (3), SPI (3), `FLASH`, `USBRESET` | keyboard and console yes; the rest are board peripherals |
| `PSRAMIOC_` | 4 | arena alloc, realloc, free, stat | becomes `malloc` |
| `GPIOC_`, `PLKIOC_` | 12 | pin claims and PIO output | no hardware on a PC |

The runtime has some ninety mentions of `ioctl`, every one behind a
single `mm_gfx_open()`. The feature headers reach the kernel only
through runtime libcalls, a rule `libgate.sh` enforces because a header
that called anything else died on the board at load time. Each helper
program has two to four call sites of its own. So the seam is narrow:
one open, one ioctl, and a handful of `struct` layouts.

The wire-protocol wrinkle is that the kernel reads argument arrays
**where they lie**. `struct gfx_batch` carries pointers to up to 1,024
points and colours; `gfx_text` carries the string; the blit-rows calls
carry row buffers; `snd_buf` carries PCM. A client library has to
flatten each of those into the message and unflatten the replies
(`gfx_info`, `kbd_down`, the pixel value, rows out). That is a
per-code marshalling table, most of whose entries are a plain integer.

Beyond the ioctls, these are the other places the runtime and the
helpers touch the operating system:

* **The terminal.** `INKEY$`, `KEYDOWN` and `ON KEY` take the tty raw
  (`mm_raw_hold`), give it back around `INPUT`, and restore it through
  `atexit` and the signal handlers. Windows has no termios; the
  runtime already says so and names `_kbhit`/`_getch`.
* **Processes.** Three `fork`+`execvp` sites and one `execv` in the
  runtime (players, loaders, `SYSTEM`), `waitpid`, and `kill(SIGINT)`
  to stop a player. `mmedit`'s F2 is an `execv` of `/usr/bin/cc`.
* **The control FIFO** `/tmp/.playctl` and the kind file
  `/tmp/.playkind`, whose semantics were pinned to the Fuzix kernel by
  `fifotest.c` and are not POSIX.
* **Pipes** carrying decoded sprites from `loadpng -s` and
  `loadimage -s` into the program.
* **The clock**: `clock_gettime` on the host already, the kernel's
  microsecond counter on the board.
* **Pins.** `SETPIN` stores straight to registers with one claim
  ioctl; the host build already gets stubs, so `SETPIN` programs run
  under the gates. That stays.
* **Sockets.** The `WEB` family uses Fuzix's BSD-shaped sockets with a
  PC3-specific `IPPROTO_TLS`.

---

## 4. An architecture for the host

### 4.1 Three shapes, one recommended

**A. A device server and a client library.** One process, call it the
display server, owns the MiniFB window, the miniaudio device and the
keyboard state, and listens on a local socket. Programs link a client
library whose `mm_sys_open()` connects and whose `mm_sys_ioctl(code,
arg)` sends a message and waits for the reply. The 77 codes are the
protocol; `pico_ioctl.h` is the shared header on both sides, and
`ioctlcheck.sh` keeps guarding it.

**B. An in-process device library.** Each program opens its own
window. Simplest to start and the fastest per call, but it makes the
helpers impossible in their current shape: `playmp3` is a process that
outlives the program that started it, `loadjpg` draws on the program's
screen from another process, and two programs can share the display as
they do on the board. B means folding all of that into the runtime and
accepting one window per program. It remains the right shape for a
single-program toy or a headless test rig.

**C. Emulate the machine.** Rejected. There is no RP2350 emulator with
HSTX and PSRAM, and the MicroPython emulator's design reached the same
verdict for the same reason: the hardware layer is thin and the fidelity
comes from running the same source.

A is recommended, for five reasons:

1. The kernel's device code ports into it nearly verbatim. The
   drawing primitives, the framebuffer management, the CSI console
   engine, the synths and the PCM ring are plain C over byte buffers;
   only the scanout, the DMA and the I2S program are hardware.
2. The multi-process design survives unchanged. Players, loaders,
   `SYSTEM`, sprites down a pipe, two BASIC programs at once.
3. One window, whatever is running, as the board has one screen.
4. **Per-connection state is per-process state.** The kernel keys the
   framebuffer write target, the PCM owner, the console mirror owner
   and the pin claims by `p_tab` or pid, and releases them from
   `pagemap_free` when the process dies. The server keys the same
   things by socket connection and releases them on disconnect. That
   is one code path for every "the program died holding X" case, which
   is where the board's hardest bugs were.
5. The ioctl surface is already a numbered, gated, documented
   protocol. Nothing has to be designed; it has to be marshalled.

The cost is latency. A `/dev/sys` ioctl costs 1.3 to 1.5 µs on the
board; a Unix-socket round trip is 5 to 15 µs on Linux and more on
Windows. The mitigation already exists: the runtime batches points and
spans (`GFXIOC_PIXELS`, `RECTS`) precisely because syscalls were
expensive, and a 312-point line is one crossing. The one hot reader is
`saveimage`, which does 76,800 `GETPIXEL` calls for a 320×240 screen;
it should move to `GFXIOC_BLITRDR`, the rows-out reader that now
exists. A read-only shared-memory mirror of the framebuffer is a later
option if anything else needs bulk readback.

### 4.2 Two consoles, mapped one to one

The PC3 already has two consoles. `console_putc` sends every byte to
the CH340 uart **and** to the screen, both ends parse ANSI, and a
program that draws its own text in a graphics mode asks for
`PICOIOC_CONMIRROR 0` to stop the screen half. The USB keyboard feeds
the tty ring and the `KEYDOWN` table; a serial terminal's keys feed
the tty only.

On a PC this maps without invention:

| board | host |
|---|---|
| serial console (TeraTerm) | the terminal the program was started from: stdin, stdout, `mmedit`, the shell |
| screen console | the server window, when a program has opened the display |
| `console_putc` mirroring to both | the client library tees console output to the server while a window is open; `CONMIRROR` means exactly what it means today |
| USB keyboard feeding tty and `KEYDOWN` | window keys go to the server, which updates the `KEYDOWN` table and forwards typed characters to the client that owns the display |
| serial keys feeding the tty | terminal stdin, as now |

The client's `mm_rd1()`, the one read primitive under `INKEY$`, polls
two descriptors, the terminal and the socket, instead of one. `INKEY$`
therefore works whichever has focus, and `KEYDOWN` needs the window
focused in the same way that the board's `KEYDOWN` needs the keyboard
in the PC3, not in the serial terminal. Ctrl-C in the window is
forwarded as `0x03` into the same stream, so the runtime's existing
raw-mode handling sees it.

### 4.3 The display with MiniFB

MiniFB is a small MIT-licensed C library with Win32, X11, Wayland and
macOS back ends. It opens a window, takes a 32-bit ARGB buffer per
frame (`mfb_update_ex`), scales it to the window through a viewport,
and delivers keyboard, character, mouse and resize events through
callbacks. Its keyboard callback reports each key **press and release
with a modifier mask**, which is what `KEYDOWN` needs.

The server keeps the framebuffers in the kernel's own formats, because
`display.c`'s primitives expect them: the 640×480 1bpp console, the
4bpp 320×240 mode 7 (MMBasic `MODE 2`), the BBC modes 0 to 5, and the
off-screen framebuffers 2 and 3 that `FRAMEBUFFER` and `LAYER`/`MERGE`
use. Each 60 Hz tick it does what the core1 fill loop does, expanding
the live framebuffer through the palette into the mode's raster (640×480
or 1024×768, pixel-doubled where the kernel doubles) into the MiniFB
buffer. `GFXIOC_VSYNC` and `VSYNCTRY` wait for that tick;
`display_in_blanking` becomes a flag it sets. `mfb_set_target_fps` and
`mfb_wait_sync` pace it.

MiniFB is single-threaded and wants its event pump called regularly,
so the server's main loop is `poll()` over the client sockets with a
timeout to the next frame, then `mfb_update_ex`. No threads are needed
on the display side; the audio callback (below) runs on miniaudio's
thread and shares state through the same ring the kernel's IRQ used.

Under WSL this works today: WSLg on this machine exports `DISPLAY=:0`,
a Wayland display and the X11 development headers, all present.

### 4.4 Sound with miniaudio

miniaudio is a single-header, public-domain audio library with WASAPI,
DirectSound and WinMM on Windows, ALSA, PulseAudio and JACK on Linux,
and Core Audio on macOS. Its model is a **pull callback** that asks for
N frames, which is exactly the role of the kernel's DMA completion
interrupt. The kernel's three fill modes go into that callback with
their code intact: the BBC synth (`SNDIOC_SOUND`/`ENV`, 22,050 Hz), the
PCM ring that `playmp3`, `playwav`, `playflac` and `playmod` write into
(`SNDIOC_PCM*`), and the MMBasic synth that `PLAY SOUND` and `PLAY
TONE` drive at 44,100 Hz (`SNDIOC_MMCMD`, the `playsnd.c` transplant).
Open the device once at 44,100 Hz, 16-bit stereo, and resample the
other rates through miniaudio's resampler rather than reopening.

Ownership keeps the kernel's rules: one PCM stream, one owner, a
second `PCMOPEN` refused with `EBUSY`, the stream released when the
owner's connection drops (the server's version of
`sound_mm_owner_gone`). `SNDIOC_PCMWAIT` becomes a blocked reply.

The players lose two constraints and keep their shape. The FPU
discipline (exactly one process may hold live FP state, because the
kernel saves no FP context) and the process-heap-not-arena rule are
board facts with no PC equivalent. They stay separate programs because
the runtime's `PLAY MP3` spawns one and expects it to keep playing
after the program ends. `PLAY STOP` is a `SIGINT` on Linux; on Windows
it becomes a server message that makes the player's next `PCMWRITE`
fail, which is the shape `MMSTOP` already has for the synth. The
control FIFO and the kind file go the same way: relayed through the
server, which removes the Fuzix-only FIFO semantics from the picture on
both platforms.

WSLg carries audio to Windows through a PulseAudio RDP sink
(`/mnt/wslg/PulseServer` is present here). Expect on the order of a
hundred milliseconds more latency than native. That is invisible for
MP3 and MOD playback and audible for `PLAY SOUND` effects in a game;
native Linux and native Windows have no such penalty.

### 4.5 The keyboard, and `KEYDOWN`

The recommended path is the one the MicroPython emulator proved:
turn the host's key events into 8-byte HID boot reports and feed them
to `kbd_decode.c` unchanged. Layouts, AltGr specials, lock keys,
auto-repeat timing, `ON KEY` and the six-slot held-key table behind
`KEYDOWN(0..8)` then come from the same 534 lines that run on the
board, and `kbdsync.sh` keeps the copies identical.

One difference from the emulator needs a decision in phase 2. SDL
scancodes **are** HID usage codes, so `kbd_sdl.c` feeds them straight
through. MiniFB reports GLFW-style virtual keys (`KB_KEY_A`,
`KB_KEY_F1`, `KB_KEY_KP_0`, ...), which the host OS has already mapped
through the user's layout. So the server needs a table from MiniFB key
to HID usage (about 110 entries) that assumes a US layout, and the
decoder should run with its US table, because the host has applied the
real layout once already. Letters and function keys are unambiguous;
punctuation on non-US layouts is where this can go wrong, and the
fallback is MiniFB's character callback, which delivers the typed
character with shift and AltGr applied, for printable keys, while the
key callback drives `KEYDOWN` and the specials. Test on a UK layout
before choosing.

Lock states come from MiniFB's modifier mask (`KB_MOD_CAPS_LOCK`,
`KB_MOD_NUM_LOCK`), so the per-keyboard num-lock memory
(`PICOIOC_NUMLOCK`) has nothing to remember.

If per-key events from the window ever prove insufficient, the
lower-level routes are Linux evdev (`/dev/input`, needs the `input`
group and is absent under WSLg) and Windows Raw Input (`WM_INPUT`
scancodes). Both hand over physical scancodes, which would make the
US-table assumption go away. For `KEYDOWN` as MMBasic specifies it, a
held set with modifiers and locks, the MiniFB callbacks are enough.

### 4.6 Windows

* **Sockets.** Windows 10 build 17063 and later support `AF_UNIX`
  stream sockets, so the protocol code is shared. Some MinGW headers
  lack `afunix.h`; declaring `sockaddr_un` locally is the known fix.
  Named pipes are the fallback.
* **Processes.** The four spawn sites in the runtime and the one in
  `mmedit` go behind an `mm_spawn()` seam: `fork`/`execvp` on POSIX,
  `_spawnvp` or `CreateProcess` on Windows; `waitpid` becomes
  `WaitForSingleObject`; `kill(SIGINT)` becomes the server message
  described above.
* **The console.** `SetConsoleMode` with `ENABLE_VIRTUAL_TERMINAL_INPUT`
  and `ENABLE_VIRTUAL_TERMINAL_PROCESSING` gives raw input and ANSI
  output on Windows 10 and later; `ReadConsoleInput` replaces the
  `VMIN=0` read; `GetConsoleScreenBufferInfo` replaces `TIOCGWINSZ`.
  `mmedit`'s `shim.c` is the one file that changes, and the runtime's
  raw-hold path gets the same shim. The runtime already carries
  `_WIN32` branches for directories and `conio.h`.
* **`bcrun`.** It maps the VM's memory at a fixed address because a
  program address is a machine address; `VirtualAlloc` with an address
  does the same. `mprotect` for executable code is not needed: there
  is no native code on x86.
* **`ccbc`.** The driver forks the passes and relies on `cc1` and
  `cc2` reading back from their own standard output through a
  descriptor opened `O_RDWR`. On Windows that is `CreateProcess` with
  file handles. The passes and `cpp` themselves read and write files
  and port as they are.
* **Toolchain.** MinGW-w64 under the same CMake tree as Linux is the
  least machinery. MSVC is possible, the runtime was written with it
  in mind, but it means a second build description.
* **Distribution.** One zip: the server, `bcrun`, `cc`, `cpp`, the
  three passes, `mmbc`, `mmedit`, the loaders and players, and the
  `include` directory `ccbc` needs.

### 4.7 WSL

Everything in the Linux column applies. The toolchain the user already
builds with is there (gcc 15.2, cmake, make, python3), WSLg supplies
the window and the audio, and the only WSL-specific facts are the
audio latency noted above and that frame pacing comes from a timer, not
a real vertical blank.

---

## 5. `cc` versus a native compiler

First, the frame. `cc` was never the toolchain for the helpers. On the
board `playmp3`, `loadjpg`, `mmedit`, `mmbc` and `bcrun` itself are
cross-compiled with `arm-none-eabi-gcc`; on a PC they are built with
gcc, clang or MinGW. The Compiler Kit compiles one thing: **the C that
the translator emits from a BASIC program**, plus hand-written C by
people using the PC3 as a computer. The question is therefore which
engine runs a translated program on the PC, and both engines already
exist on the host: `make check` builds every test with gcc, and
`fcc/fcctests.sh` runs the same programs through `cc` and `bcrun`.

A finding from this review: the translator's `--fcc` output, the
dialect written for `cc`, also compiles under gcc against the plain
runtime and produces byte-identical output (`tests/t1.bas` checked
against its `.expected`). The runtime's `MM_FCC` define is the
`bcrun`-hosted shape and is not meant for a standalone gcc build, but
the emitted C is one dialect. So a program can be translated once and
run under either engine.

### `cc` to bytecode, under `bcrun`

Pros:

* **It is the board's compiler.** Same dialect, same limits, same
  runtime boundary. A program that runs on the PC compiles on the PC3.
  The traps the trees record, an array of an anonymous struct, a
  14-character identifier, a libc call `bcrun` cannot resolve, a
  missing prototype that shifts arguments, all show up on the PC
  instead of on the board. This is the "different is worse than
  missing" principle applied to the development machine.
* **No toolchain on the user's PC.** `cc1` is 6,283 lines and
  compiles the 137 KB of C the solar-eclipse program becomes in a
  blink. The bundle is self-contained, and identical on Windows and
  Linux.
* **Sandboxed.** The VM bounds-checks every access to `mem[]`. A bad
  translation is a runtime error, not a crashed host process.
* **The objects travel.** A `.bc` built on the PC runs on the board's
  `bcrun`; the format is the same and versioned. The host `cc2` can
  also emit the board's native Thumb (the qemu differential harness
  relies on that), so the PC becomes a cross-compiler for the PC3
  with no ARM toolchain installed. That is a cross-development story
  the gcc path cannot offer.
* **It is fast enough.** The recorded numbers, same program, same
  inputs: MMBasic on the board 12.5 s, MicroPython 8.77 s, `bcrun`
  bytecode on the board 9.19 s, the board's native Thumb 2.3 s, and
  **`bcrun` bytecode on a Ryzen 0.10 s**. The host interpreter beats
  the board's native code by more than twenty times before any host
  compiler is involved.

Cons:

* **Fifty times slower than gcc on the same PC.** The eclipse is
  0.10 s interpreted against 0.002 s native. The Thumb back end that
  gives the board its speed cannot run on x86-64, and a native x86-64
  back end would be a new emitter of the order of `backend-thumb.c`
  (4,224 lines) plus executable-memory handling on two operating
  systems. Not worth it while the interpreter outruns the board.
* **The dialect is C89 minus bitfields**, with 14-character
  identifiers and locals scoped to the function. Irrelevant for
  translated BASIC, a real limit for hand-written C.
* **The VM address space is 128 KB on the host (96 KB on the board).**
  A program with large arrays hits it. It can be raised on the host,
  but a raised ceiling is a divergence from the board and should be
  chosen deliberately.
* **Debugging a program means `bcdump`.** gdb, valgrind and the
  sanitizers see `bcrun`, not the program. The runtime itself is
  native and fully debuggable.

### gcc, clang or MSVC, native

Pros:

* Speed, as above, and no VM ceiling.
* The reference implementation. The `.expected` files are blessed
  under gcc; `mmb_runtime.c` compiled by gcc is the truth the `bcrun`
  path is measured against, and gdb, valgrind and ASan reach both the
  runtime and the program. This is where runtime bugs get found.
* Zero setup on Linux and WSL.

Cons:

* **It accepts what the board refuses.** Every trap in the list above
  passes silently under gcc, and the runtime boundary differs: the
  gates link glibc, so a header calling a libc function `bcrun` does
  not bind passes on the PC and dies on the board at load. `libgate.sh`
  exists because that happened.
* **A toolchain on the user's Windows PC.** MinGW-w64 or MSVC is fine
  for developers and a wall for the audience the book is written for.
  The middle path is to bundle **tcc**: C99, a few megabytes,
  Windows and Linux, roughly two to three times slower than gcc at
  `-O2` and still an order of magnitude faster than the interpreter;
  `libtcc` can even compile in memory and run at once. It is LGPL,
  which is fine for a separately invoked compiler.
* No sandbox.
* Two runtime shapes to keep in step, though that is already the
  case: `bcrun_mm.c` syncs from the master in `mmb2c`.

### Recommendation

Make `cc` to `bcrun` the primary engine on every platform, because it
is faithful, needs nothing installed, and is already twenty times
faster than the hardware it is standing in for. Offer gcc or clang as
`--native` where a compiler is present, for speed and for debugging
the runtime. **Whichever engine runs the program, always compile with
`cc` as well**; it is instantaneous, and it is what stops the PC from
accepting a program the board will reject. Revisit a host code
generator only if the interpreter ever becomes the bottleneck for a
real program, which nothing recorded so far suggests.

---

## 6. Work plan

Each phase ends with a gate that exists or is named here, following
the trees' own rule of harness first.

**Phase 0, the repository and source sharing.** No third copies. The
FUZIX `pc3` tree and the MicroPython tree are submodules; CMake builds
`mmbc`, `cpp`, `cc0`/`cc1`/`cc2`, `ccbc`, `bcrun`, `mmedit`, the loaders
and the players from their present locations, with the display-less
runtime, and today's gates run green from the new tree. The kernel
files needed by the server (`display.c`'s drawing core, `console.c`'s
engine, `sound.c`'s synths and ring) get a **seam refactor in the
FUZIX tree**, the pattern `hdmi.c` and `hdmi_priv.h` set in the
MicroPython port, so the kernel and the server compile one file each,
with a hash gate like `kbdsync.sh` for anything that must be vendored.

**Phase 1, the display server on Linux.** The server process, the
client library, `pico_ioctl.h` as the wire header, the 28 graphics
codes plus `INFO`, `MODE`, `VSYNC` and `CONMIRROR`, MiniFB presentation
of every mode and the off-screen framebuffers. Gate: `samples/*.bas`
run and look right, and `SAVE IMAGE` output diffed against captures
taken on the board, the `bmptest.sh` method. The pixel-exact
`blitharness` keeps running unchanged.

**Phase 2, the keyboard.** HID report synthesis from MiniFB events into
`kbd_decode.c`; `KEYDOWN`, the merged `INKEY$` stream, `ON KEY`,
Ctrl-C; the layout decision from section 4.5, tested on a UK layout.
Gate: `keydump` (the board's own key probe) built against the client
library, and a game (`breakout.bas`, `picofrog`) playable.

**Phase 3, sound.** miniaudio device, the three fill modes, PCM
ownership, `PLAY SOUND`/`TONE`/`MP3`/`MOD`/`WAV`/`FLAC`, the completion
interrupts whose latencies were measured on the board. Gate: the
`sndharness` and `pcmpace` programs, and A/B by ear against the board.

**Phase 4, the programs around the program.** `loadjpg`, `loadpng`,
`loadimage`, `saveimage` (moved to rows-out readback), `SPRITE
LOADPNG` down its pipe, `SYSTEM`. Gate: the image tests in `samples/`
against board captures.

**Phase 5, the editor and the driver.** `mmedit` with a configurable
compiler command, `cc -r prog.bas` end to end, relocatable install
paths in `ccbc` (it hard-codes `/usr/lib/cc`, `/usr/bin/cpp` and
`/usr/bin/mmbc`). Gate: the pty harness that already drives `mmedit`,
and the book's build-and-run walkthrough followed on a PC.

**Phase 6, Windows.** MinGW-w64 build, the console shim, the spawn
seam, `AF_UNIX`, `VirtualAlloc` in `bcrun`, `ccbc` on `CreateProcess`,
the zip. Gate: phases 1 to 5's gates on Windows 11.

**Phase 7, networking.** BSD sockets and Winsock behind the `WEB`
family, TLS through mbedtls (the library the kernel already uses).
Gate: `retic.bas` and the `WEB` samples.

Optional, after: the `--native` engine and a bundled tcc; a
shared-memory framebuffer for bulk readback; a virtual pin panel or a
USB-to-I2C bridge (MCP2221A or FT232H) so the I2C samples can talk to
real QWIIC sensors from a PC.

Rough sizes:

| piece | new | ported from the trees |
|---|---|---|
| server: loop, protocol, marshalling, connection state | 1,500 | |
| server: display core, console engine, palette, fonts | | 1,400 + font data |
| server: MiniFB presentation, mode rasters, vsync | 400 | |
| server: audio callback, ring, ownership | 300 | 700 |
| server: keyboard, HID synthesis, key table | 400 | 534 |
| client library, `mm_sys_*`, tee, merged read, spawn seam | 800 | |
| Windows console and process shims | 500 | |
| build, install layout, `ccbc` path handling | 300 | |
| **total** | **about 4,200** | **about 2,600 plus fonts** |

---

## 7. Risks and open questions

* **MiniFB's key model.** Virtual keys, not scancodes; the US-table
  assumption in section 4.5 is the one design decision this review
  could not settle from the source, and it needs the phase 2 test.
* **One thread for the window.** MiniFB's pump and the socket loop
  share a thread by design (`poll` with a frame timeout). Any blocking
  operation in a request handler, `PCMWAIT` or `VSYNC` for instance,
  must be a deferred reply, not a wait.
* **Timing fidelity.** `PAUSE`, `TIMER` and `SETTICK` use the host
  clock and are fine. Frame pacing is a 60 Hz timer, not a display's
  vertical blank, and under WSLg the compositor adds its own latency.
  A game that races `VSYNC` will feel slightly different.
* **What the PC does not model.** The 84-block process pool, PSRAM
  arena limits, the FPU rule and the native-code size cliff are why
  several designs in the trees are what they are. On the PC none of
  them bind, so a program that fits on the PC may not fit on the
  board. The honest answer is to document it and, later, to report the
  existing native size model's estimate rather than to emulate memory
  pressure.
* **Hardware that is simply absent.** GPIO, I2C, SPI, PIO, WS2812,
  one-wire, PWM, pulse measurement, counting inputs, the RTC registers
  and flash slots. The runtime already answers "not available" on a
  host, and that stays, per "different is worse than missing". The
  optional bridges above are the only way to make any of it real.
* **Licences.** The translator, runtime and the editor port carry the
  mmb2c `LICENSE` (BSD-style, Geoff Graham and Peter Mather). The
  kernel code the server would port is GPL v2 (`Kernel/COPYING`), and
  the Compiler Kit has no licence file of its own in
  `Applications/CC`, so assume FUZIX's GPL v2 until checked upstream.
  MiniFB is MIT, miniaudio is public domain or MIT-0, the dr_libs and
  picojpeg are public domain, upng is zlib. Keeping the server (GPL
  kernel code) and the client programs (BSD runtime) as separate
  executables talking over a socket is a clean boundary; `bcrun` is
  already a GPL program compiling in BSD runtime source, which is
  compatible. Give each directory its own licence file.
* **The bytecode VM's memory ceiling** (section 5) and whether the
  host build should raise it.

---

## 8. What the user ends up with

A terminal and a window. In the terminal, `mmbc prog.bas`, `cc -r
prog.bas`, `bcrun prog.bc`, `mmedit prog.bas` with F2 compiling and
running, exactly the commands the PC3 manual teaches. In the window,
the PC3's screen: the same modes, palette, fonts, sprites, layers and
framebuffers, drawn by the same code. The keyboard works for `INKEY$`
from either and for `KEYDOWN` from the window. `PLAY` makes sound. A
`.bc` from the PC runs from the SD card on the board, and the host
`cc2` can emit the board's native code without an ARM toolchain.

For the developer: gdb and valgrind on the runtime, the whole gate
suite on one machine, a screen and a keyboard for the samples that
"are built but never run" today, and a platform for teaching from the
book without a board on the desk.

---

## Appendix A: the ioctl codes

From `pico_ioctl.h` at `e48007c85`. The number space is flat; two names
sharing a number is a bug `ioctlcheck.sh` catches.

```
PICOIOC_FLASH      0x0001   GFXIOC_MODE        0x0003   GFXIOC_PAL         0x0004
GFXIOC_BLIT        0x0005   SNDIOC_SOUND       0x0006   SNDIOC_ENV         0x0007
SNDIOC_QUIET       0x0008   PICOIOC_ADVAL      0x0009   PSRAMIOC_ALLOC     0x000A
PSRAMIOC_FREE      0x000B   PSRAMIOC_STAT      0x000C   PSRAMIOC_REALLOC   0x000D
GFXIOC_INFO        0x000E   GFXIOC_PIXEL       0x000F   GFXIOC_COLOUR      0x0010
GFXIOC_GETPIXEL    0x0011   GFXIOC_RECT        0x0012   GFXIOC_BITMAP      0x0013
GFXIOC_PIXELS      0x0014   GFXIOC_RECTS       0x0015   GFXIOC_FBSEL       0x0016
GFXIOC_FBOPEN      0x0018   GFXIOC_VSYNC       0x0019   GFXIOC_TEXT        0x001A
GFXIOC_SCROLL      0x001B   PICOIOC_USBRESET   0x001C   GFXIOC_FONTINFO    0x001D
GFXIOC_MAP         0x001E   GFXIOC_MAPCTL      0x001F   PICOIOC_LIBM       0x0020
SNDIOC_PCMOPEN     0x0021   SNDIOC_PCMWRITE    0x0022   SNDIOC_PCMSTAT     0x0023
SNDIOC_PCMCLOSE    0x0024   SNDIOC_PCMOWNER    0x0025   PICOIOC_RTCREG     0x0029
PICOIOC_I2COPEN    0x002A   PICOIOC_I2CCLOSE   0x002B   PICOIOC_BOARD      0x002C
PICOIOC_I2CXFER    0x002D   GFXIOC_FONTADDR    0x0031   GFXIOC_BLITRD      0x0032
GFXIOC_FBCOPY2     0x0033   GFXIOC_MERGE       0x0034   GFXIOC_SCROLL2     0x0035
GFXIOC_FONTDEF     0x0036   PICOIOC_NUMLOCK    0x0037   PICOIOC_CONMIRROR  0x0038
GFXIOC_BLITR       0x0039   GFXIOC_BLITRDR     0x003A   SNDIOC_MMCMD       0x003B
SNDIOC_MMSTOP      0x003C   PICOIOC_KEYDOWN    0x003D   GFXIOC_VSYNCTRY    0x003E
SNDIOC_PCMWAIT     0x003F   PICOIOC_RANDOM     0x0044   GPIOC_PIOOUT_BUF   0x053D
```

The SPI, keyboard-map, pin-claim (`PLKIOC_`) and PIO (`GPIOC_`) codes
complete the 77; they are board peripherals the server would answer
with "not available".

## Appendix B: what was checked for this review

* Line counts with `wc -l` over the paths in section 2.
* `python3 mmb2c.py tests/t1.bas --fcc`, then `gcc -std=gnu99 -I.
  t1.c mmb_runtime.c -lm`: builds, runs, output identical to
  `tests/t1.expected`. With `-DMM_FCC` it does not build (`usleep` is a
  `bcrun` libcall in that shape), which is expected.
* `Applications/CC/host-armm0/bcrun` exists and is the gate executor;
  `bcrun.c` executes native code only under `__arm__`/`__thumb__`.
* WSL on this machine: gcc 15.2, cmake, make, python3;
  `/usr/include/X11/Xlib.h`, `/usr/include/pulse/pulseaudio.h`,
  `/usr/include/alsa/asoundlib.h`; `DISPLAY=:0`,
  `WAYLAND_DISPLAY=wayland-0`, `PULSE_SERVER=unix:/mnt/wslg/PulseServer`.
* The ioctl families counted from `pico_ioctl.h`: 28 `GFXIOC`, 11
  `SNDIOC`, 17 `PICOIOC`, 4 `PSRAMIOC`, 9 `GPIOC`, 3 `PLKIOC`.
