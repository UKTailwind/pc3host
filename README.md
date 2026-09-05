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

## Where things stand

Phase 0 is done: the build tree, today's gates running against it, and
the first kernel seam (the display driver's portable half compiles and
runs without the kernel). The device server, the client library, the
keyboard, sound and Windows are phases 1 to 6 in the review; a PicoMite
on a USB serial link for real I/O is phase 8.
