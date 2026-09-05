# Phase 5: the editor and the driver

What REVIEW.md's work plan asked of phase 5: `mmedit` with a
configurable compiler command, `cc -r prog.bas` end to end, relocatable
paths in `ccbc`; gate, a pty harness driving `mmedit` and the manual's
walkthrough followed on a PC. Dated 2026-09-05.

## What was already there

Phase 2 did the paths when the package needed them: `ccbc` finds its
passes and headers beside itself under `PC3_HOST`, `cc -r` runs the
sibling `bcrun`, `mmedit`'s F2 runs the sibling `cc`, and the object
files the board builds from the same sources are byte-identical.
`packaging/test-deb.sh` proves `cc -r` end to end from an installed
copy on every package. What phase 5 added is the configurable command
and the harness.

## MMEDIT_CC

F2 in `mmedit` is "save, exit, compile and run": the editor execs the
compiler with `-r` and the file. On the board that is `/usr/bin/cc`; on
a PC it is the `cc` beside the editor. `MMEDIT_CC` now names another -
a wrapper that adds flags, a different build of the compiler, a script
that records what it was asked - and it is run the same way, `-r` and
the file. The line the editor prints before it goes says which. The
board's arm of the code is untouched.

## The pty harness

There was no harness in the tree, despite the review's belief; the
editor's earlier host testing left nothing behind. `tests/editor/
mmedit_pty.py` is one now, and it follows the manual's walkthrough
(`mmedit prog.bas`, then F2) through a real pseudo-terminal, 80 by 40,
the editor's controlling tty:

1. `mmedit hello.bas` on an empty file. The harness waits for the status
   line's `F1:Save`, types `Print "hello from mmedit"`, presses F1 as
   the sequence a terminal sends for it (`ESC O P`, which is what
   MMInkey takes), and checks the editor exited cleanly and the file
   holds the line.
2. `mmedit hello.bas` again, then F2 (`ESC O Q`). The editor saves,
   prints `cc -r hello.bas`, execs the compiler named by `MMEDIT_CC` -
   this build's `cc` - which translates, compiles and runs the program,
   and `hello from mmedit` arrives on the same terminal after the
   compiler's line. Exit status 0.

The first draft of the second check looked for the line followed by
`\r\n` and failed while the text was plainly on the screen: the runtime
prints `\r\n` itself and the tty turns the `\n` into another `\r\n`.
The check now matches the line at a line start after the compiler's
announcement - which also keeps it from matching the editor's own
display of the source, where the same words sit inside `Print "..."`.

The harness runs with `PC3_DISPLAY=off`: the editor asks the display
for the console mode on the way in and out, and the program prints to
the terminal, so neither needs a server, and a test must not start one.

## Gates

* **editor-pty**: the harness above, in `ctest`, against this build's
  `mmedit` and `cc`.
* **test-deb** continues to prove `cc -r` and the launcher from the
  installed layout.

## Not in phase 5

* The book's walkthrough is followed by a script, not by a reader; the
  manual's `mmedit` chapter matches what the script sees, and that is
  as far as a machine can take it.
* `mmedit` still asks the display for the console mode through
  `pc3_sys_open()`; with no server and `PC3_AUTOSTART` at its default a
  bare `mmedit` on a PC starts one. That is the intended shape for a
  desktop and a surprise in a script, which is why the harness turns
  the display off.
