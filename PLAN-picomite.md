# PicoMite programs on pc3host: the plan

24 October 2026. The goal: the PicoMite programs the CMM2 runs under
`OPTION PICOMITE` run unchanged on pc3host, on Linux and on Windows.
The CMM2's work is the reference. See its `docs/Game_Porting_Log.md`
and `docs/PicoMite_CMM2_Mode_Map.md`, and its `Src/Rgb121.c`,
`Src/Mono.c` and `Src/PicoMode.c` (CMM2V1.5, in
D:\Dropbox\VSCode-STM32).

## 1. The programs

The CMM2 plays seven PicoMite games, and 120 of the 147 graphics
programs from TheBackShed thread 17161 ran there. pc3host, from each
game's untouched original (`cc`, then a 10 s headless run).

When this plan was written (24 Oct):

| Game | pc3host then | What stopped it |
|---|---|---|
| Chuckie Egg | runs, title screen right | 14 `PLAY BBC SOUND/ENVELOPE` lines commented out |
| Elite | translation fails | "unterminated do block" at 4276; `LIBRARY LOAD` of elite_lib.bas |
| Exile | runs, then stops | `MM.VER` check ("firmware too old"); `PEEK(VAR ...)` x8; 2 TILEMAP forms; `MEMORY INPUT`; `MM.INFO(PSRAM SIZE)`; a PicoMite CSUB (EXILETICK) |
| PETSCII Robots | translation fails | `DefineFont 9` (the PicoMite allows it) |
| Picanoid | runs, then stops | `MM.VER` check; `PLAY BBC` x12; `DEVICE(MOUSE ...)`; `MM.INFO(PSRAM SIZE)`; `RESOLUTION` |
| Picofrog | translation fails | `DefineFont 9` |
| Prince of Pico | translation fails | the raw source: it needs its own `build.py`, and assets its setup fetches |

After the first two batches of gaps (FUZIX 5da662e37 and 6a6ae2cb5):

| Game | pc3host now | What stops it |
|---|---|---|
| Chuckie Egg | runs, no line left out | - |
| Elite | translates (1 line out) and compiles | the first `Draw3D`; `MM.INFO(WRITEBUFF)` and `MEMORY COPY` on the framebuffer |
| Exile | refused | the CSUB (deferred) |
| PETSCII Robots (the CMM2's original) | runs, 11 lines out | `RUN`; `DEVICE(WII ...)` |
| Picanoid | stops at once | `RESOLUTION`; 4 x `DEVICE(MOUSE ...)` |
| Picofrog | runs, 10 lines out | `MM.INFO(PIN n)`, `MM.INFO(SYSTEM I2C)`, `RUN`, a `)` after `PIXEL`'s last argument (the firmware ignores what follows an argument) |
| Prince of Pico | refused | needs its `build.py` output |

Elite also needed the compiler fixed: its 35,241 lines of C passed the
token stream's 15-bit line numbers (FUZIX 30c529d9d).

The CMM2's copy of thread 17161's programs was never committed. It is
to be extracted again from the 13 pages.

## 2. The display: the PicoMite HDMI build's, which is the PC3's

The PC3 is a PicoMite HDMI build, so there is one mode table to follow,
with no VGA/HDMI option. These are the modes at 640x480 (other
resolutions are phase 4):

| MODE | Size | Pixels | Layers shown |
|---|---|---|---|
| 1 | 640x480 | 1 bit, with TILE colour pairs (8 x font height) | N ORed with L |
| 2 | 320x240 | 4 bits, a 16-colour palette (MAP) | N, L over it, T over that |
| 3 | 640x480 | 4 bits | N, L over it |
| 4 | 320x240 | RGB555 | N, L over it (an RGB transparent colour) |
| 5 | 320x240 | 8 bits, a 256-colour palette | N, L, T |

**How the CMM2 does it:**
- The pages are the PicoMite's framebuffers, byte for byte: N, L, F, 2
  and T. In 4-bit the left pixel is in the LOW nibble, and a row is
  (width + 1) / 2 bytes.
- At the end of each frame, N, L and T are merged into what is shown,
  each layer's transparent colour showing what is under it.
- `FRAMEBUFFER CREATE`, `CREATE 2`, `LAYER [t]`, `LAYER TOP [t]`,
  `WRITE`, `COPY`, `MERGE`, `WAIT` and `CLOSE` act as the PicoMite's
  graphics/FrameBuffer.c does.

**pc3host now:**
- MODE 1, 2 and 3 only.
- N, F and L, with no 2 and no T.
- L is the LCD model: it shows only on an explicit `FRAMEBUFFER
  MERGE`. The PicoMite's VGA and HDMI builds show it at every frame,
  and picofrog, robots and Prince of Pico are written for that.
- The 4-bit format puts the left pixel in the HIGH nibble.
- No `TILE`.

The board kept the layer out of the display for want of 40K of SRAM. A
PC has the room.

**The proposed shape (PC only; the board's kernel is untouched):**
- **The pages live in shared memory** that pc3d creates and bcrun maps
  into the program's address space (shm on Linux, a file mapping on
  Windows).
  - `MM.INFO(WRITEBUFF)`, PEEK, POKE and `MEMORY COPY` then reach the
    PicoMite's own bytes. Elite copies its dashboard rows this way.
  - Drawing can write pixels directly instead of one socket message
    each.
- **pc3d's presenter merges** N, L and T into the window at each frame,
  as the CMM2's merge does (MODE 1: N OR L in tile colours).
- **The runtime's FRAMEBUFFER**, under PC_HOST, becomes the PicoMite
  VGA/HDMI semantics. The board keeps its own.

## 3. Translator and runtime gaps

Both translators; the board gains these too.

- Done: `DefineFont #n` for 1-16 (#9 replaces built-in font 9, as on
  the PicoMite); `PLAY BBC SOUND` and `ENVELOPE`; `PEEK(VAR x, n)`;
  `MM.INFO(PSRAM SIZE)` and `LCDPANEL`; `LIBRARY LOAD` as the first
  statement (Elite's "unterminated do block" was its missing library);
  `a() = b()`; `MM.INFO(HEAP)`; `POKE DISPLAY VRES` at the mode's own
  height; `dir` as a variable name.
- Next: `RESOLUTION 640` (MODE 1 at the one resolution there is),
  `MM.INFO(SYSTEM I2C)` ("I2C": the PC3's clock bus), `MM.INFO(PIN n)`,
  `RUN`, the stray `)`, `DEVICE(MOUSE ...)`, `MEMORY INPUT`, and the
  two TILEMAP forms Exile uses.
- `Draw3D`, for Elite: the firmware's 3D engine, about 1,850 lines of
  reference (catmap category 3 until now).
- `MM.VER` answers the PicoMite version mmb2c follows, on the PC and
  on the board alike (decided 24 Oct). The Fuzix release stays in the
  boot banner.
- CSUBs: PicoMite ARM code cannot run on a PC. **Decided 24 Oct: an ARM
  emulator in bcrun, deferred.** Until then a CSUB is refused, as now.

## 4. Phases

1. The gaps in section 3 that stop a game outright: DefineFont 9,
   `MM.VER`, `PLAY BBC`, Elite's DO, `MM.INFO(PSRAM SIZE)`, `PEEK(VAR)`,
   `MEMORY INPUT`. That gets robots, picofrog, chuckie and picanoid
   through translation, as far as the current display lets them go.
2. The display:
   - shared-memory pages;
   - the per-frame merge;
   - the PicoMite FRAMEBUFFER (N, L, F, 2, T) in the PicoMite's byte
     layout;
   - MODE 1-5 at 640x480, with TILE and MAP.
3. `LIBRARY LOAD` (Elite), the TILEMAP forms, `DEVICE(MOUSE)` in the
   window, `RESOLUTION`; Prince of Pico from its built source.
4. The other resolutions, as the table in the CMM2's Mode Map has them:
   720x400, 800x600, 848x480, 800x480, 1024x600, 1280x720 and
   1024x768.

Each phase has a gate: the games' originals run headless, with no
error and a screenshot compared to a golden. Thread 17161's programs
become a corpus of their own.
