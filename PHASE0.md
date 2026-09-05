# Phase 0: the repository, the build, and the display seam

What REVIEW.md section 6 asked of phase 0, and where each part stands.
Dated 2026-09-05.

## What is here

```
pc3host/
  CMakeLists.txt        builds every host tool from the FUZIX tree
  ext/FUZIX             the FUZIX fork, pc3 branch, as a submodule
  tests/seam/           the display core compiled without the kernel
  REVIEW.md             the review this implements
  PHASE0.md             this file
```

Nothing is copied. Every C file is compiled where it lives in the
FUZIX tree, so the host build and the board build are the same sources
and cannot drift. The MicroPython tree is not a submodule after all:
the one file the review wanted from it, `kbd_decode.c`, is vendored
byte-identical in the FUZIX platform directory already, with
`kbdsync.sh` guarding the copy, and the SDL emulator shims are
reference material rather than code this tree compiles.

## Building

```
git submodule update --init          # once; no need for FUZIX's own submodules
cmake -S . -B build
cmake --build build -j8
ctest --test-dir build               # the gates, section "Gates" below
```

`build/bin` then holds `mmbc`, `cpp`, `cc0`, `cc1`, `cc2`, `ccbc`,
`bcrun`, `bcdump`, `dumptokens`, `mmedit`, `loadjpg`, `loadpng`,
`loadimage`, `saveimage`, `playmp3`, `playwav`, `playflac`, `playmod`,
`playsnd` and `display_seam`. Twenty targets, all from unmodified
sources; the compiler passes take exactly the flags of
`Applications/CC/Makefile.host`, so the objects they write are the
objects the gates have always run.

A developer with a live FUZIX checkout builds against it rather than the
pinned submodule:

```
cmake -S . -B build -DFUZIX_ROOT=/home/me/src/FUZIX
```

The submodule is pinned to the FUZIX commit this tree was last proven
against. Moving it forward is a deliberate act: `git -C ext/FUZIX
checkout <commit>` then commit the pointer, with the gates green.

## Gates

`ctest` runs today's gates against this build's binaries. They live in
the FUZIX tree and needed three one-line changes to take the binaries
by environment instead of assuming their own build directories:
`fcctests.sh` and `fccbuild.sh` honour `BIN`, `cgate.sh` honours
`MMBC`. Without those variables they behave exactly as before.

| test | what it proves | source |
|---|---|---|
| `seam-display` | the kernel's display core compiles and behaves without the kernel | `tests/seam/display_seam.c` |
| `fcc-gate` | every `tests/*.bas` through `mmb2c.py --fcc`, this `cc0`/`cc1`/`cc2`, this `bcrun`, against `.expected` | `mmb2c/fcc/fcctests.sh` |
| `fcc-gate-mmbc` | the same with this `mmbc` as the translator | same, `MMB2C=` |
| `cgate` | this `mmbc` emits byte-identical C to `mmb2c.py`, plain and `--fcc`, over tests and samples | `mmb2c/mmbc/cgate.sh` |
| `libgate` | the program-side headers call only what `bcrun` can bind | `mmb2c/libgate.sh` |
| `gcc-check` | the reference path: every test built with gcc against `mmb_runtime.c` | `mmb2c/Makefile check` |

Result on 2026-09-05, against the live FUZIX tree with the seam in
place: all six pass. `fcc-gate` 90 programs in 34 s, `fcc-gate-mmbc`
90 in 28 s, `cgate` 26 s, `libgate` under a second, `gcc-check` 112
programs in 175 s, `seam-display` instant.

The fcc gates and `cgate` are serialised by a resource lock because
`cc2` writes `.symtmp` in its working directory. Not wired in yet, and
worth adding when phase 1 starts building on them: `hosttest/ctest.sh`
(the C89 conformance run, 165 of 175, needs the `c-testsuite`
checkout beside FUZIX) and `hosttest/all.sh`.

## The display seam

`Kernel/platform/platform-rpipico/display.c` was one 2,202-line file
mixing the drawing primitives with the HSTX scanout. It is now:

| file | lines | holds |
|---|---|---|
| `display.c` | 1,366 | framebuffer ownership and selection, `FRAMEBUFFER COPY` and `MERGE`, the mode table and palette state, the RGB888 colour model, and every primitive: pixel, rect, batches, blit, bitmap, text, both scrolls |
| `display_hstx.c` | 906 | video timing, HSTX and TMDS programming, where the three framebuffers live (SRAM and the PSRAM window), the PSRAM checks, the expansion LUT, the DMA interrupt, core1's fill loop and watchdog, scanout start and stop, the blanking waits, the stack sentinel, `display_init`, and the five hooks |
| `display_priv.h` | 70 | the seam: `enum gexp`, the two shared variables, the raster constants, the five hooks, `disp_who_parent` |

`display.h` is unchanged; nothing else in the kernel knows the file was
split. The split was made by a script that located every slice by a
unique anchor and asserted the count of every substitution, so no line
moved silently and no edit was a no-op.

The contract, in the order `display_gfx_mode()` calls it:

1. `disp_hw_mode_prepare(raster)`: choose the raster; stop the scanout
   if it changes (return 1) or wait for the top of blanking if not (0).
2. `disp_hw_mode_tables(exp)`: rebuild whatever the expander needs for
   the new palette.
3. The core clears the framebuffer and stores `gfx_exp`, the handover
   flag core1 reads every scanline.
4. `disp_hw_mode_handover(exp, raster)`: the barrier, then the timing
   switch. On the PC3 this is `__dmb(); tim = tim_next;`. On a host it
   is nothing.
5. `disp_hw_mode_finish(rebuild)`: restart the scanout if step 1
   stopped it.

And `disp_hw_palette_changed(exp)` after `MAP SET`, `MAP RESET` and
`VDU 19`, which on the PC3 rebuilds the LUT as before. The one
behavioural nuance is deliberate and documented in both halves: the
one-file version wrote `gfx_exp` inside the same three lines as the
barrier; now the core writes it and the hook follows with the barrier.
Same stores, same order, one function call between two of them.

`disp_who_parent(who)` answers "a child of the framebuffer's owner draws
where the owner draws". In the kernel it is the macro `(who)->p_pptr`,
so the hot path (`display_fb_enter`, once per graphics ioctl) is
byte-for-byte what it was. A host build defines `PC3_HOST` and supplies
a function.

### Proof

**The kernel.** Rebuilt from `build/` with the split in place; it
compiles and links, and the placement list still puts the cold display
functions in flash (the four mode hooks join `display_gfx_mode` there;
`disp_hw_palette_changed` stays in RAM for `gfx_lut_rebuild`'s reason).
Section sizes against the pre-split build:

| | before | after | delta |
|---|---|---|---|
| text | 629,664 | 629,744 | +80: the hook prologues and epilogues |
| data | 512 | 512 | 0 |
| bss | 668,424 | 668,432 | +8: the pending-timing pointer |

**The host.** `display_seam` compiles `display.c` with `-DPC3_HOST`
and no kernel header, against stand-ins for everything the seam and
`display.h` say the hardware provides, and walks the primitives: mode
7's geometry and RGB121 colour indices, a pixel there and back, the
nibble and bit orders in the framebuffer, batched points with colours,
text through the font table at scale 1 and 2, a full-screen rect as a
memset, both scrolls including wrap, MAP pending then live then reset,
the off-screen buffers with owner, child and stranger, copy, merge
with a transparent index, mode 0's 1bpp layout on the other raster,
and the console's tile colours. The hook sequence and rebuild flags
are checked from inside the stand-ins.

**What is not proven.** That the kernel still drives a monitor. The
scanout code moved verbatim and the mode-switch sequence is unchanged
in order, but only the board shows a picture, and the tree's rule is
that a kernel change is unverified until it has been flashed and seen.
The change is on the `pc3` branch and needs that pass before it is
trusted; `SAVE IMAGE` of a `MODE 2` and a `MODE 0` screen before and
after, diffed, is the cheap version.

## The other two seams

The review named `console.c` and `sound.c` as well. They are analysed
here and deliberately not cut in phase 0, for two reasons: each is a
kernel change that wants its own hardware pass, and neither is needed
until the phase that consumes it (the console engine in phase 1's
graphics-mode PRINT, sound in phase 3). Cutting them alongside the
display would put three unverified kernel changes into the user's next
flash at once.

**`console.c` (990 lines).** The portable part is the terminal engine:
the CSI parser (`do_csi`, `do_sgr`, `charout`), the tile and glyph
plotting, the scrolls, the cursor, and the graphics-mode rendering
through `display_gfx_*`. The platform part is thinner than the
display's: `console_putc` mirrors every byte to the uart and takes
`di()` around the engine; `console_init` hooks the tty; the cursor
report (CSI 6n) pushes bytes into `usbkbd.c`'s ring with `kbd_push`;
and the mirror owner is a `struct p_tab`. The cut is four hooks
(`con_hw_lock`/`unlock`, `con_hw_uart_putc`, `con_hw_input`) plus
`disp_who_parent`'s twin for the mirror owner. The one subtlety is the
interrupt discipline: the engine runs under `di()` because tty echo
reaches it from interrupt context, and a host engine runs on one thread
with no such need, so the lock must be a hook and not a no-op'd macro.

**`sound.c` (1,183 lines).** Portable: the BBC synth (`snd_fill` and the
100 Hz envelope stepper), the MMBasic synth (`mmsnd_fill`, the
polyBLEP voices, `mms_*`), the PCM ring logic and the ownership rules.
Platform: PIO I2S and DMA setup, `pcm_set_rate`'s clock divider, the
DMA interrupt, the PSRAM ring allocation, `uget` from user memory in
`sound_pcm_write`, and the scheduler poke in `sound_pcm_tick`. The cut
is a `snd_hw_*` set for the output stage and rate, with the three fill
functions becoming the portable half's contract: "fill this many
frames". miniaudio's callback is exactly that shape, which is why the
review chose it.

## FUZIX-side changes made by this phase

All on the `pc3` branch, not pushed:

* `Kernel/platform/platform-rpipico/display.c` split into `display.c`,
  `display_hstx.c`, `display_priv.h`; `CMakeLists.txt` lists the new
  file; `linker_overrides/default_text_excludes.incl` follows
  `display_init` to its new object and adds the four mode hooks.
* `Applications/mmb2c/fcc/fcctests.sh`, `fcc/fccbuild.sh`: `BIN`
  override. `Applications/mmb2c/mmbc/cgate.sh`: `MMBC` override.

The submodule pointer in this repository records the FUZIX commit that
carries them, so the pointer resolves only once FUZIX is pushed.
