# Phase 6 - Windows

The same tree, built for Windows: `cc`, `bcrun`, `mmbc`, `mmedit`, the
loaders, the players and the device server, as 26 native `.exe` files
that need nothing installed beside them. Every gate the Linux build has
runs here and passes, including the whole 90-program translator corpus,
the display and keyboard goldens, sound, and the `WEB` family over
Winsock with TLS through the machine's own certificate store.

Nothing was forked. The sources are still the FUZIX tree's, and the
board's objects are byte-identical after all of it - `stageall.sh`
compared against the v0.26 card, twice.

## How it is built

MinGW-w64, cross-compiled from Linux, so one machine builds both
packages:

    apt install mingw-w64
    cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
          -DCMAKE_BUILD_TYPE=Release
    cmake --build build-win -j8
    bash packaging/make-zip.sh          # dist/pc3host-<version>-win64.zip

MSVC was the alternative and is not needed: the review's judgement that
MinGW under the same CMake tree is "the least machinery" held. Every
program links `-static`, so a `.exe` copied anywhere runs on any 64-bit
Windows 10 or later.

## The shim, and why it is a shim

`hostshim/win32` is the POSIX surface these sources use, over Winsock
and Win32: one C file and a directory of stand-in headers that come
first on the include path. `termios.h`, `poll.h`, `sys/socket.h`,
`sys/mman.h`, `sys/wait.h`, `sys/uio.h`, `sys/ioctl.h`, `netinet/in.h`
and the rest are the shim's; `unistd.h`, `fcntl.h` and `signal.h`
include MinGW's own and add what it lacks.

The point of doing it this way is that the shared sources stay as they
are. A `tcsetattr` that clears `ICANON` is a raw console here; a
`poll()` over sockets and the terminal is `WSAPoll` plus a console
handle; `mmap` below 4G is `VirtualAlloc` walking up from 0x30000000,
which is what `bcrun`'s VM needs because a program address is a machine
address. Only five places in the shared tree carry a `#ifdef _WIN32`,
and they are the places where there is genuinely no fork to call.

**Sockets are pseudo-descriptors.** A Winsock `SOCKET` is not a C
runtime descriptor, and every program here stores both as an `int`, so
the shim hands out descriptors from 0x4000 up and translates. That is
why `read`, `write` and `close` on a socket go through `sock_read`,
`sock_write` and `sock_close` in the client library, the server and
`bcrun`: on Linux those are the plain calls, unchanged.

**The console.** Raw mode is `ENABLE_VIRTUAL_TERMINAL_INPUT` with line
input and echo off, so arrows and function keys arrive as the escape
sequences a terminal sends and `mmedit` and `INKEY$` decode them
unchanged; `VMIN` and `VTIME` decide what a read waits for, exactly as
the line discipline would; `ICRNL` turns the Enter key's CR into NL.
A console too old for virtual-terminal input falls back to the shim's
own translation of key records, which is what `tests/win/console_test.c`
exercises - it injects key records and checks the bytes that come back.

**Processes.** `pc3w_spawn` is `CreateProcess` with the two files as
the child's standard input and output, which is what `cc` needs for its
passes; `waitpid` is `WaitForSingleObject`; `kill(pid, 0)` is
`OpenProcess`. The server is started detached, with its messages in
`%TEMP%\pc3d.log` as it logs beside its socket on Linux.

**A program that is not there is not an error.** `fork` succeeds and
the `exec` fails in the child, where nothing can report it, so `PLAY`
finds out for itself and says "Sound output did not start". Windows
learns it one call earlier, and saying so would make the same program
print something different, so `mm_run_bg` returns 0 for `ENOENT` and
lets the statement speak. That is the only behavioural difference the
whole corpus turned up, and it is now closed.

## What the platform actually made us change

* **The transcendental functions.** MinGW computes `cos` with the x87
  instructions, whose answer for a right angle differs from glibc and
  from the board in the last three digits, and `tests/mathm.bas` prints
  exactly that. The FUZIX C library's own (musl) implementations are
  compiled into everything that folds or prints a float, so all three
  machines now agree bit for bit.
* **`printf`.** `__USE_MINGW_ANSI_STDIO` selects MinGW's own formatter;
  the Microsoft runtime prints `1e+005` where every other C prints
  `1e+05`.
* **Binary streams.** `_fmode` is `_O_BINARY` and the three standard
  descriptors are set binary when they are not the console, because the
  compiler passes stream a binary object through their standard output.
* **The name servers.** There is no `/etc/resolv.conf`, so the
  program-side resolver asks `NETIOC_STATUS` when the file names
  nothing - the server answers from `GetAdaptersAddresses`, and on the
  board the kernel answers from the lease. A fix for every platform, not
  a Windows branch.
* **The certificate store.** Windows keeps its roots in the ROOT store
  rather than a file, so `pc3tls.c` enumerates it there. The live test
  fetches `https://example.com` and checks for a 200.
* **The player control FIFO** is a named pipe (`\\.\pipe\pc3-playctl`),
  message mode, non-blocking - the same five steps the FIFO takes.
* **`_itoa` and `_ltoa`** are Microsoft runtime functions with three
  arguments; the front end and the preprocessor each had a one-argument
  helper of that name, renamed under `_WIN32` only.
* **`strlcpy` and `strlcat`** are in the FUZIX library and in glibc,
  and not in MinGW, so the shim carries them.
* **A path is written with backslashes.** `cc` builds its four
  intermediate names from the source's basename, and looked for `/`
  alone: given `share\examples\gfx1.bas` the whole path went into a
  64-byte buffer, truncated, and the driver then said
  `...\gfx1.ir: No such file or directory` for a file it had just
  preprocessed. The first thing anyone would hit, since a command
  prompt completes paths that way, and the corpus gate never saw it
  because it compiles bare names in a working directory. The package's
  own `selftest.cmd` found it.

## The gates, and what they say

Run from Git Bash on the machine itself; `<tree>` is `build-win` or an
unpacked zip.

    bash tests/win/corpus.sh  <tree> <tree>/gatebin <fuzix> <work>
    bash tests/win/e2e.sh     <tree> <pc3host> <work>
    bash tests/win/net.sh     <tree> <pc3host> <fuzix> <work>
    bash tests/win/window.sh  <tree> <e2e-work> <work>

| gate | what it proves | result |
|---|---|---|
| corpus | all 90 translator tests, cc.exe then bcrun | 90 pass |
| e2e | gfx1, gfxc, keys, pcmpace, play against the goldens | 7 pass |
| net | websrv, webget, UDP, CONNECT, live TLS | 9 pass |
| window | a real window, WASAPI audio, the snapshot | 2 pass |
| console_test.c | the raw console, key by key | 11 pass |

The BMP goldens are the ones the Linux gate uses, byte for byte: the
same framebuffer, the same expansion, the same file.

## What is not done

* **`mmedit` has no automatic test here.** The console shim underneath
  it is tested key by key, and the editor was driven by hand, but the
  pty harness the Linux gate uses has no Windows counterpart - a
  console-injection harness is the shape it would take.
* **No installer.** The zip is the package: unpack it and run
  `pc3.cmd`. An installer would want a signature, which is a decision
  rather than a piece of work.
* **The window is one size.** `--scale` works as on Linux; there is no
  full-screen mode on either.
