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
* `mmedit`'s F2 runs the `cc` beside it.

All of it is `#ifdef PC3_HOST` with the board's code in the `#else`.
The board's `bcrun.o`, `ccbc.o` and `mmedit.o`, compiled from before
and after with the tree's ARM flags and stripped of debug information,
are byte-identical.

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

## Not in phase 2

* `INPUT` reads the terminal's cooked line, so a program typing a name
  with the window focused still needs the terminal for that line.
* The shell's text in the window (the console engine) is still a
  later phase; the terminal is the text console.
* Mouse, and the num-lock-per-keyboard memory, which has nothing to
  remember here.
* Wayland: the window is X11 (XWayland serves on a Wayland desktop).
