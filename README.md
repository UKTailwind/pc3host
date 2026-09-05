# pc3host

The Pico Computer 3's BASIC environment, hosted on a PC: the `mmbc`
translator, the Compiler Kit `cc`, the `bcrun` runtime, the image
loaders, the players and `mmedit`, running natively on Linux, WSL and
Windows with MiniFB for the display and miniaudio for sound.

Nothing is implemented yet. Start with [REVIEW.md](REVIEW.md), which
surveys what exists in the FUZIX `pc3` tree, proposes the architecture
(a device server that plays the kernel's part, plus a client library
behind the runtime's `/dev/sys` calls), weighs `cc` against a native
compiler, and lays out the phases.

Source of truth for the shared code stays where it is: the FUZIX fork
(`Applications/mmb2c`, `Applications/CC`, `Applications/mmedit`,
`Kernel/platform/platform-rpipico`) and the MicroPython
`pico-computer-3` branch (`kbd_decode.c`, the SDL emulator shims). This
repository is meant to hold only what is specific to hosting them.
