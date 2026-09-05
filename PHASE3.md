# Phase 3: sound

What REVIEW.md section 4.4 and the work plan asked of phase 3. Dated
2026-09-05.

## What works

`PLAY SOUND`, `PLAY TONE` with its completion interrupt, `PLAY VOLUME`
and `PLAY STOP` play through the PC's sound card; `PLAY WAV`, `MP3`,
`FLAC` and `MODFILE` spawn the same players the board spawns and they
feed the same PCM ring; the BBC `SOUND`, `ENVELOPE` and `QUIET` calls
answer as the kernel's do. One stream, one owner, a second player
refused with EBUSY, the stream handed back when its owner's connection
drops, `SNDIOC_PCMWAIT` a blocked reply, the pid `PCMOWNER` reports the
real one so `PLAY STOP`'s signal reaches the player. Every sample in the
tree that plays a sound now runs; before this phase the first `PLAY
SOUND` raised "Sound output did not start".

## How

**The kernel's sound.c, cut once.** `sound.c` (1,183 lines) was one
file of synths and DMA. It is now `sound.c`, the portable half - the
BBC synth, the PCM ring, the MMBasic synth and the ownership rules,
over blocks of 64 stereo frames - and `sound_hw.c`, the PC3's output
stage: the PIO I2S program, the two chained DMA channels and their
completion interrupt, the clock divider, and the three things the
portable half needs from the kernel proper (a copy from a process, the
process table, and the sleep behind `PCMWAIT` with its scheduler poke).
`sound_priv.h` is the contract, the pattern `display_priv.h` set in
phase 0: five hooks (`snd_hw_rate`, `snd_hw_copyin`, `snd_hw_pid_alive`,
the lock pair), one entry (`sound_fill_block`), one accessor
(`sound_pcm_queued`). The kernel's ARM objects before and after: text
23,121 bytes against 21,500 + 1,627 = 23,127, data 45 against 21 + 24,
bss 1,264 against 739 + 525. A relayout, six bytes of call overhead.
The linker's flash list gained `sound_hw.c.o` beside `sound.c.o`; the
interrupt's fill functions keep their RAM residency through `SND_FAST`,
which is `__not_in_flash_func` on the board and nothing on a PC.

**miniaudio plays the blocks.** `server/sndhw.c` answers the hooks and
opens one playback device at 44,100 Hz, 16-bit stereo, through
miniaudio (`ext/miniaudio`, a submodule; its backends are `dlopen`'d at
run time, so the package depends on no audio library). The device's
pull callback asks the core for blocks at whichever rate the core has
set - 22,050 for the BBC synth, 44,100 for the MMBasic synth, a file's
own rate for PCM - and runs them through miniaudio's linear resampler
to the device rate. A rate change is a resampler ratio, applied at the
next block; the device is never reopened. `--audio null` opens
miniaudio's null backend, which consumes frames in real time and plays
nothing, and is what the gates run; a machine with no audio device
gets the null backend and a note, so a player still finishes.

**The lock.** On the board the fill runs in the DMA interrupt and the
core's lock is `di()`. Here the fill runs on miniaudio's thread and the
lock is a recursive mutex, held by the callback around the fill and by
the server around every call into the core, so the two never touch the
synth at once. Recursive because the core takes it again inside
`sound_cmd`.

**Owners are tokens.** The core knows an owner as a 16-bit pid, which a
Fuzix pid is and a Linux pid is not. So each connection gets a 16-bit
token, never zero and never one in use, and the core owns and reaps by
token; `PCMOWNER` translates back to the connection's real pid.

**`PCMWAIT` is answered from the loop.** The kernel's tick wakes a
player when the ring has drained to its mark. The server's `poll()`
loop does the same: a waiting connection is not read until it has its
answer, and while one is outstanding the loop runs every 2 ms rather
than at the frame.

**The players reach the server.** `playmp3`, `playwav`, `playflac`,
`playmod`, `playsnd` and `pcmpace` opened `/dev/sys` by name; they now
go through `utils/pc3sys.h`, as the image programs already did, which
is the same two calls on the board and the client library on a PC. All
six ARM objects are byte-identical before and after. The players'
control FIFO and kind file (`/tmp/.playctl`, `/tmp/.playkind`) work as
they are on Linux; relaying them through the server is for the Windows
phase.

**The client.** Eleven codes join `pc3_sys_ioctl`: the fixed-width
structures go over as their bytes, `PCMSTAT` comes back as one, and
`PCMWRITE`'s samples are the request's payload - the pointer in
`struct snd_buf` never crosses the socket. The kernel's `bcrun` already
translated `PCMWRITE` for 32-bit programs, so a C program compiled by
`cc` plays too.

## Gates

* **seam-sound** (`tests/seam/sound_seam.c` and `.sh`): `sound.c`
  compiled against stub hooks and driven directly - the ownership
  rules, the ring, mono expansion, underrun counting, the BBC square's
  pitch (220 rising crossings in half a second at pitch 89), the note
  queue's depth - and then its MMBasic synth's 5 seconds of 440 Hz
  against `sndharness`, which is `playsnd`'s renderer standing alone:
  **byte-identical from frame 2048 to the end**, once the synth's volume
  ramp has met the harness's fixed volume. The kernel synth is
  `playsnd`'s renderer moved into the interrupt, and this is the proof.
* **e2e-display** gained two sound sections against the null backend:
  `pcmpace` at `playsnd`'s old pattern reports no underruns while
  feeding and drains 16K in four or five 20 ms polls (93 ms of audio);
  `play.bas` plays a `SOUND`, a `TONE` whose interrupt fires, stops,
  and plays a WAV twice with a `STOP` between - the second `PLAY WAV`
  proves the stop released the stream.

`pcmpace`'s *final* underrun count is not zero on a PC, and would not be
on the board: it counts the empty blocks between the ring running dry
and the program's next 20 ms poll noticing. The number that matters is
the one while feeding.

## Verified, and not

The kernel builds (`make -C build fuzix`) and the seam test proves the
portable half's behaviour. The board has not been flashed with the
split kernel; when it is, the cheap check is a `PLAY SOUND` and a
`PLAY MP3`, and `sndharness`'s WAV played through `playwav`. The real
device was heard from only as a backend name and a paced ring: under
WSLg the server picked PulseAudio and `pcmpace` fed it two seconds
without an underrun. Listening is the user's part.

## Not in phase 3

* The `ADVAL` sound-queue selectors (BBC BASIC's), which the server
  does not answer yet.
* Windows: the players' FIFO and kind file, and `PLAY STOP` as a
  signal, are Linux-shaped; section 4.4 says how they change.
* Latency under WSLg is PulseAudio-over-RDP latency, on the order of a
  hundred milliseconds. Native Linux has no such penalty.
