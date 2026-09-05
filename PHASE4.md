# Phase 4: the programs around the program

What REVIEW.md's work plan asked of phase 4: `loadjpg`, `loadpng`,
`loadimage`, `saveimage` moved to rows-out readback, `SPRITE LOADPNG`
down its pipe, `SYSTEM`; gate, the image tests. Dated 2026-09-05.

## What was already there

Three of the four items turned out to be done by earlier phases and
only wanted proving. The loaders have opened `/dev/sys` through
`pc3sys.h` since phase 1 and draw through `GFXIOC_PIXELS` into the
server. `SPRITE LOADPNG` runs `loadpng -s` and reads the sprite back
down a pipe through the runtime's native `mm_run_pipe`, which the host
`bcrun` has always carried. `SYSTEM` is `mm_run_exec`, a fork and an
`execvp`, and the sweep had already shown it working: `varroom.bas`
prints the output of `free` and `ps`. The image round-trip tests in the
FUZIX tree - `imgtrip`, `imgloop`, `imgm1` and its variants, `wtest`,
`rtest`, `saveimg` - all pass against the server. Two more need
pictures that are not in the tree (`tiger.bmp`, the `g*.bmp` set) and
were left.

## saveimage reads rows

`saveimage` read the screen with one `GFXIOC_GETPIXEL` per pixel:
76,800 ioctls for a 320x240 screen, a tenth of a second on the board
and, on a PC where every ioctl is a round trip to the server, 1.7
seconds; 3.4 for the 640x480 modes. It now reads bands of 32 rows
through `GFXIOC_BLITRDR`, the rows-out reader the kernel already had -
the framebuffer's bytes as they lie, 4-bit or 1-bit indices in
`display.c`'s packing (high nibble left, bit 7 left) - and turns the
indices into colours through `GETPIXEL` once per distinct index, at the
first pixel that carries it. The kernel resolves the live palette
exactly as it did for every pixel before, so the file is the same file:

| screen | GETPIXEL | rows out | files |
|---|---|---|---|
| MODE 2, 320x240 | 1,710 ms | 3 ms | identical |
| MODE 0, 640x480 | 3,419 ms | 4 ms | identical |
| the console | (unchanged path) | | identical |

The console has no rows-out reader - its colours are per cell, not per
index - and a kernel older than `BLITRDR` refuses the first read; both
fall back to the pixel loop, which is what the program always did. The
board gets the same program: eight ioctls a screen instead of 76,800.
It is cross-compiled but not yet run there.

## Gates

* **images** (`tests/images/run.sh`): the nine round-trip tests against
  a headless server, their output - `sum` of every file they save and
  reload, with `ls -l` lines reduced to size and name - against goldens
  kept here. The goldens were blessed with the GETPIXEL `saveimage` and
  reproduced by the rows-out one, which is how the rewrite was proved
  before the timing above was even taken.
* **bmptest**: the FUZIX tree's own gate for `loadimage -s`, the sprite
  decoder, checked against an independent implementation of the
  reference's index expression on boundary colours. It runs on the host
  and always did; it is in `ctest` now.
* **corpus-display**: the whole FUZIX test corpus with a display,
  passing when the set of non-identical programs is exactly the known
  fourteen in `tests/sweep/corpus.known` - four file limitations and
  ten pin tests. A name arriving is a fault; a name leaving is progress
  and a shorter list. Its first run found two arrivals, `play` and
  `strargs`: their goldens, blessed on a host with no sound, expect the
  one legitimate `PLAY SOUND` to fail with "Sound output did not
  start", and since phase 3 it plays. Limitations of the files, and now
  on the list with that reason.

## Not in phase 4

The review's gate was "against board captures", and there are none in
the tree. The goldens here are the host's own; they pin the PC's
behaviour against itself and against the GETPIXEL reader, not against
the board. A capture from the board of the same tests would close that
gap and is a five-minute job with the board on the desk.
