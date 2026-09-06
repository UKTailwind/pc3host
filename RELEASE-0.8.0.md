# pc3host 0.8.0 - the Pico Computer 3's BASIC, on a PC

Two packages, one for Linux and one for Windows. Both carry the same
thing: the Pico Computer 3's own toolchain, built from the machine's own
sources, running on a PC.

You write MMBasic, `cc` compiles it, and it runs in a window that is the
PC3's screen - the kernel's display code, its keyboard decoder and its
sound engine, compiled into a small server that plays the part the
kernel plays on the board. A program that runs here runs on the board,
and it draws the same pixels: the graphics tests are compared against
images captured from the machine itself.

What is new in 0.8.0 is the Windows package. Until now this was Linux
only.

| | |
|---|---|
| `pc3host_0.8.0_amd64.deb` | Debian, Ubuntu, Mint and their kin |
| `pc3host-0.8.0-win64.zip` | Windows 10 or later, 64-bit |

## Linux

```
sudo dpkg -i pc3host_0.8.0_amd64.deb
```

Everything but the display server is statically linked, so it installs
on any distribution of that age or newer; the server needs the X11
libraries, which a desktop already has. It puts itself under `/opt/pc3`
and links the names a PC has no other use for into `/usr/bin`.

The compiler is called `cc`, as it is on the board, and that name is
deliberately not taken from your system. Use `pc3` to get a shell with
the PC3's tools first on the PATH, or `pc3cc` to call the compiler
directly:

```
pc3                                   # a shell with the tools first
cc -r /opt/pc3/share/examples/gfx1.bas
```

## Windows

Unpack the zip anywhere - your Documents folder is fine - and run
**`install.cmd`** inside it. It puts the tools on your account's PATH
and tells Windows that a `.bc` file is a program. No administrator
rights are needed, nothing is copied into Windows itself, and
`uninstall.cmd` beside it undoes both. **Open a new command prompt
afterwards**: the one you have still holds the old environment.

To try it without changing anything, run `pc3.cmd` instead, which opens
a prompt with the tools on its PATH for as long as that window is open.

Windows will not know the programs and will say so the first time: they
carry no signature. Right-click the zip, tick **Unblock** in its
Properties before extracting, and you will be asked less. A machine
running Smart App Control, or under a Device Guard policy, refuses
unsigned programs outright and there is nothing in the package that can
change that.

## The first five minutes, on either

```
cc -r hello.bas          build it and run it
cc hello.bas             build it: hello.bc
bcrun hello.bc           run what you built
mmbedit hello.bas        the editor; F2 saves, builds and runs
```

On Windows a built program is also `.\hello.bc` in a command prompt, or
a double click in Explorer. It will not run as a bare `hello`: a `.bc`
is not a Windows executable, it is a file that `bcrun` runs, the way the
`#!` line has the board run it.

The editor is `mmbedit`, not `mmedit`: MMEdit is Jim Hiley's Windows IDE
for MMBasic and this is not it.

Things to try, wherever the package put its examples (`/opt/pc3/share`
or `share\` in the folder you unpacked):

```
cc -r share/examples/gfx1.bas        a picture
cc -r share/examples/keydemo.bas     the keyboard
cc -r share/examples/pc3bench.bas    what each kind of thing costs here
cc -r share/examples/samples/breakout.bas
```

The Windows package also has **`selftest.cmd`**: it compiles the eclipse
predictor - 3,213 lines of real astronomy - with the shipped compiler,
checks its output to the last digit, and then opens the window.

## What is in the box

The compiler and its passes, the BASIC translator, the bytecode runtime,
the editor, the image loaders, the four audio players and the display
server. Beside them: 83 sample programs, the 112 test programs with the
output each must produce, the whole translator source, the manuals, and
the test scripts.

## What is not

The board's hardware. There are no pins on a PC, so `SETPIN`, `PIN`,
`I2C`, `SPI`, `PWM`, the counting inputs and the real-time clock
registers answer "not available" rather than pretending. Everything
else - graphics, sound, the keyboard, files, and the `WEB` family over
your machine's own network - is real.

Timing is the other difference worth knowing. Frame pacing is a timer
rather than a monitor's vertical blanking, and a graphics statement here
is a message to the display server where on the board it is a register
write, so a program that races the frame will feel slightly different.
`pc3bench.bas` measures it on the machine in front of you.
