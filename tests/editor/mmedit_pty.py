#!/usr/bin/env python3
"""The editor, driven through a pseudo-terminal the way a person drives
it - the manual's own walkthrough, followed on a PC:

    # mmedit prog.bas          F2
    cc -r prog.bas
    ...
    hello from prog

Two runs.  The first opens an empty file, types a program and presses
F1 (save and exit); the file must hold the line.  The second opens the
saved file and presses F2 (save, exit, compile and run): the editor
execs the compiler named by MMEDIT_CC with -r and the file, and the
program's output must appear on the same terminal.  F1 and F2 go as
the sequences MMBasic's MMInkey takes, ESC O P and ESC O Q, which is
what a real terminal sends for those keys.

    mmedit_pty.py <mmedit> <cc> <work dir>
"""
import fcntl
import os
import select
import struct
import subprocess
import sys
import termios
import time

F1 = b"\x1bOP"
F2 = b"\x1bOQ"
LEGEND = b"F1:Save"          # the status line, once the screen is drawn


def spawn(argv, env, cwd):
    """the program on a fresh 80x40 pty; returns (Popen, master fd)"""
    master, slave = os.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 80, 0, 0))

    def child():
        os.setsid()
        fcntl.ioctl(slave, termios.TIOCSCTTY, 0)

    p = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave,
                         preexec_fn=child, env=env, cwd=cwd, close_fds=True)
    os.close(slave)
    return p, master


def read_until(fd, p, needle, timeout):
    """gather output until needle appears or the program ends"""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                break
            if not d:
                break
            buf += d
            if needle and needle in buf:
                return buf, True
        elif p.poll() is not None:
            # drained and gone
            try:
                while True:
                    r, _, _ = select.select([fd], [], [], 0.05)
                    if not r:
                        break
                    d = os.read(fd, 4096)
                    if not d:
                        break
                    buf += d
            except OSError:
                pass
            break
    return buf, needle in buf if needle else False


def main():
    mmedit, cc, work = sys.argv[1:4]
    os.makedirs(work, exist_ok=True)
    prog = os.path.join(work, "hello.bas")
    env = dict(os.environ, TERM="vt100", MMEDIT_CC=cc, PC3_DISPLAY="off")
    fails = 0

    def check(what, ok):
        nonlocal fails
        print(("pass  " if ok else "FAIL  ") + what)
        if not ok:
            fails += 1

    # ---- F1: type a line, save and exit -------------------------------------
    open(prog, "w").close()
    p, fd = spawn([mmedit, prog], env, work)
    out, drawn = read_until(fd, p, LEGEND, 5)
    check("the editor drew its screen", drawn)
    os.write(fd, b'Print "hello from mmedit"')
    time.sleep(0.3)
    os.write(fd, F1)
    out, _ = read_until(fd, p, None, 5)
    rc = p.wait(timeout=5)
    os.close(fd)
    check("F1 left the editor cleanly", rc == 0)
    text = open(prog, "rb").read()
    check("the file holds the typed line", b'Print "hello from mmedit"' in text)

    # ---- F2: save, exit, compile and run -------------------------------------
    p, fd = spawn([mmedit, prog], env, work)
    out, drawn = read_until(fd, p, LEGEND, 5)
    check("the editor drew its screen again", drawn)
    os.write(fd, F2)
    # The line as the PROGRAM prints it starts a line of its own; the
    # editor's copy of the source shows it inside Print "...".  The tty
    # turns the runtime's \r\n into \r\r\n, so match up to the text.
    out, ran = read_until(fd, p, b"\nhello from mmedit", 60)
    rc = p.wait(timeout=10)
    os.close(fd)
    announced = out.find(b"cc -r ")
    check("F2 announced the compiler", announced >= 0)
    check("the program ran and printed on the terminal",
          ran and out.find(b"\nhello from mmedit") > announced)
    check("the compile-and-run exited 0", rc == 0)
    if fails:
        sys.stdout.write(out[-600:].decode("latin-1"))
    print("mmedit_pty: " + ("FAILED" if fails else "all passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
