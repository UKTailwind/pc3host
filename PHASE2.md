# Phase 2: the keyboard, and where things are installed

What REVIEW.md section 6 asked of phase 2, plus the relocatable paths
the Linux package needed. Dated 2026-09-05.

## What works

A program's `INKEY$`, `KEYDOWN` and `ON KEY` see the keys typed into the
PC3 window, decoded by the PC3's own keyboard decoder, alongside
whatever is typed into the terminal it was started from. Ctrl-C in the
window stops the program as it does in the terminal. A program with no
terminal at all, started from a desktop or a script, still gets the
window's keyboard.

## How

**The decoder is the board's.** `kbd_decode.c` is the 534 lines that
turn a USB keyboard's HID boot reports into console bytes, lock states,
auto-repeat and the six-slot held-key table behind `KEYDOWN`. It is
compiled into `pc3d` unchanged from the FUZIX platform directory, where
it is vendored byte-identical to the MicroPython port's. Its five
platform calls (`server/keyboard.c`) are a byte sink, a millisecond
clock, and three no-ops.

**MiniFB's keys become HID reports.** Every press and release from the
window's keyboard callback is queued during MiniFB's event pump and
processed after it: a MiniFB key is mapped to a HID usage, the boot
report's six slots and modifier byte are kept as a boot keyboard keeps
them, and the report goes to the decoder. The MicroPython emulator did
this with SDL, whose scancodes are HID usages; MiniFB's keys are
GLFW-style names the host has already put through its layout, so a
table stands between them (`usage_of`).

**Layouts.** The table places keys where a US keyboard has them and the
decoder runs with the layout the keyboard really has, UK by default as
on the board (`--keymap`, and `OPTION KEYBOARD` through
`PICOIOC_KBDMAP`). This works because MiniFB names a key by the
unshifted keysym of the physical key under the host's layout, so
shift-2 arrives as the 2 key with shift held and the UK table makes it
a quote as the board would. Two UK keys need care: the key left of Z
yields backslash, which under the UK layout is sent as HID 0x64, the
UK key's own code; and MiniFB has no key for the UK # key at all, so a
key MiniFB reports as unknown is taken from its character callback
instead. **This needs trying on a UK keyboard**, which is what
`pc3d --keylog` is for: it prints every key event, its usage and the
byte it became.

**X11 auto-repeat** arrives as a release and a press of the same key in
one batch; the queue recognises the pair and drops it, so the repeat a
program sees is the decoder's own, 250 ms then every 50 ms.

**The key channel.** A program's runtime opens a second connection to
the server and sends `PC3_KEYCHAN`; from then on the server writes the
decoder's bytes into it, one write per event so an arrow's `ESC [ A`
arrives whole. The newest channel gets the keys, as the foreground
program gets the console on the board; when it closes, the one before
it has the keyboard again; bytes typed while no one listens wait in
the server, as they wait in the kernel's ring. A program that never
reads does not stall the display: the write is non-blocking and a full
pipe drops, as a full ring drops.

**One read primitive.** The runtime reads every key through `mm_rd1()`,
and under `PC3_HOST` that is `pc3_rd1()` in the client library: a
`poll()` over the terminal and the key channel, honouring the VMIN and
VTIME the runtime set on the terminal so the escape-sequence wait still
works, and raising `SIGINT` on a Ctrl-C from the window as the tty's
`ISIG` does. When there is no terminal the runtime treats the window as
the console (`mm_raw_hold`), and reassembles a sequence without a wait
because the server delivered it whole (`mm_inkey`).

**`KEYDOWN`** is the decoder's held-key table, read through the same
snapshot the kernel's dispatch takes.

## Where things are installed

On the board the compiler is `/usr/bin/cc`, its passes are in
`/usr/lib/cc`, and the runtime spawns `saveimage` by name. On a PC
`/usr/bin/cc` belongs to the system's compiler. So under `PC3_HOST`:

* `bcrun` puts its own directory first on the PATH at startup, so the
  runtime's `execvp` of `saveimage`, `loadjpg` and the rest finds the
  ones beside it.
* `cc` finds its passes and headers in `../lib/cc/` beside its own
  directory, and `cpp`, `mmbc` and `bcrun` beside itself; `-r` runs
  `bcrun` by that path rather than through the object's `#!` line, and
  the line it writes is `#!/usr/bin/env bcrun`.
* `mmbedit`'s F2 runs the `cc` beside it.

All of it is `#ifdef PC3_HOST` with the board's code in the `#else`.
The board's `bcrun.o`, `ccbc.o` and `mmbedit.o`, compiled from before
and after with the tree's ARM flags and stripped of debug information,
are byte-identical.

## What a person found that the test could not

The first real user reported that `KEYDOWN` worked perfectly and
`INKEY$` answered only when a key was held long enough to auto-repeat.
Reproduced by typing through the X server into `keydemo.bas`: eight
slow taps, one shown. `KEYDOWN` drains the console queue before it
answers, as MMBasic's does; the demo asks it six times a pass, and on a
PC each ask is a round trip to the server, which answers when its frame
is done, the same instant it hands over the keyboard's bytes. So a tap
typed during one call was drained by the next, every time. The
automated test never saw this because `pc3key` injects keys between the
program's calls, not during them.

The fix is in the runtime under `PC3_HOST`: the kernel's one-ioctl
snapshot of the held-key table is kept for two milliseconds, so a burst
of `KEYDOWN` calls costs one drain and one crossing, which is what it
costs on the board, where a call is microseconds. Eight taps, eight
shown. The board's `mm_keydown` is untouched.

## Two dialects, one flag

`brownian.bas` through the packaged `cc` produced dozens of errors from
`cc1` on correct C: the translator has two output dialects, gcc's with
compound literals and the Compiler Kit's without, and the board's
`mmbc`, built with `MMBC_ARENA`, defaults to the second while the PC's
build, the gates', defaults to the first. The driver ran it with no
flag. Under `PC3_HOST` it now passes `--fcc`; the board's call is the
`#else`. With that, every sample in the tree and the solar eclipse
compile through `cc`, and the eclipse reproduces its expected output in
73 ms. The build tree now carries `lib/cc/{cc0,cc1,cc2,include}` beside
`bin/` as the installed tree does, so `build/bin/cc` works and the
sweep can be run before a package is made.

## The package, and the first machine it met

`packaging/make-deb.sh` builds the Debian package with every tool
statically linked, so the tools run on any Linux of the architecture
regardless of its C library. The first version linked the server
statically too, X11 libraries and all, and on the first real machine
the window appeared blank and the server died. The cause, found by
looking for `dlopen` in the static libX11: MiniFB creates a blank
cursor at window open, libX11 answers a cursor creation by loading the
system's libXcursor at run time, and a statically linked glibc can only
`dlopen` libraries built against the same glibc, which the build
machine's was and the target's was not. So `pc3d` is an ordinary
dynamic program, C++ runtime included, and the package depends on the
X11 libraries, libstdc++6 and glibc 2.34, Ubuntu 22.04's or later.
Getting to 2.34 rather than 2.38 took two steps: no `_GNU_SOURCE` in
the server's sources, because with glibc 2.38+ headers it renames
`strtol` to a symbol only 2.38 has; and the system's libstdc++ rather
than the static one, whose objects on this box reference `arc4random`
(2.36) and the same renamed `strtoul`.

The server now installs handlers for the fatal signals (`server/crash.c`)
that print the signal and the stack's return addresses before dying,
so a crash on a machine the author cannot see leaves a note that
`addr2line` turns into lines. `pc3d --version` names the build.

## Tests

`e2e-display` gained `keys.bas`: a program with no terminal runs
against the headless server while `pc3key` (`tools/pc3key.c`, which
sends `PC3_INJECT` events the server treats exactly as the window's)
holds `a` for a tenth of a second, taps the up arrow, types shift-b
and `q`. The program writes what `INKEY$` and `KEYDOWN` saw:

```
 97
held     1    97
 128
 66
 113
```

The first draft of the test held the key for 300 ms and got two `a`s,
which was the decoder repeating after 250 ms, exactly as it should.

## The sweep

Every sample and every test program was run against a headless server
(`tests/sweep/run-samples.sh`, four seconds each, the screen saved
afterwards; `tests/sweep/run-corpus.sh`, the board-shaped bcrun against
the `.expected` files). Two faults were the host's to fix; the rest is
later phases.

**The print that vanished.** With a display, `layer.bas` lost the two
lines it prints after trapped errors. The cause was in the runtime, not
the display: while `ON ERROR` is armed a `PRINT` is buffered and
committed at the statement boundary, and the commit wrote each byte
"to the glyph engine, or else to the tty". In a graphics mode the glyph
engine takes the byte, so the line was painted and never reached the
console, whereas the same `PRINT` unarmed went through `mm_putc`, which
honours `OPTION CONSOLE` and writes both. One `mm_sink()` now carries
the option for both paths. The hosted build found it because the tty is
its whole console; on the board the same line was missing from the
serial side under `OPTION CONSOLE BOTH`. The corpus with a display is 78
identical, 2 differ (mminfo's lock state and printat's cursor escapes,
both the file's limitation), 10 fail (the pin tests, phase 8).

**Every SETTICK program crashed.** `settick`, `tempr`, `tickpause`,
`udprecv` and `websrv` died with SIGSEGV at address 0x400b0025, with a
display or without, but only when built by the `cc` driver: the same
program through the gate script ran. The address is in the RP2350's
TIMER0 register block. The driver preprocessed with `-DMM_PC3`, the
board's define, under which `mmb_int.h` and `mmb_wait.h` read TIMER0
directly for the tick clock and `mmb_gpio.h` writes the pin registers;
on the board a program address is a machine address and that is right,
on a PC it is unmapped memory. The gate script uses `-DMM_FCC`, under
which those headers call the runtime's natives, and the host bcrun is
that runtime. So under `PC3_HOST` the driver defines `MM_FCC`; the
board's arm is the `#else` and its `ccbc.o` is byte-identical. The
settick torture sample now passes all five phases on the host. The
lesson generalises: anything a program-side header does *directly to
hardware* is a host fault waiting to happen, and the define is the
switch.

**What the rest of the sweep says.** The 33 programs that finished
cleanly did what they say; the ten still running at the limit had drawn
the right screen (Breakout and PicoVaders title pages, the Brownian
particles, the orbit, the greyscale map). Eighteen stopped with a clean
error from a later phase: `Pin cannot do that` and `I2C2 cannot open`
(phase 8), `WIFI not connected` and the TLS and DNS errors (phase 7),
and files that exist only on a board. Five sound programs stopped with
`Sound output did not start` after the spawned player printed
`/dev/sys: No such file or directory` - the players open the device
themselves, and that is phase 3's work. Three more wanted MOD files
under `/root`.

## Not in phase 2

* `INPUT` reads the terminal's cooked line, so a program typing a name
  with the window focused still needs the terminal for that line.
* The shell's text in the window (the console engine) is still a
  later phase; the terminal is the text console.
* Mouse, and the num-lock-per-keyboard memory, which has nothing to
  remember here.
* Wayland: the window is X11 (XWayland serves on a Wayland desktop).
