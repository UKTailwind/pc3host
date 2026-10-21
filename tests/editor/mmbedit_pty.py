#!/usr/bin/env python3
"""The editor, driven through a pseudo-terminal the way a person drives
it - the manual's own walkthrough, followed on a PC:

    # mmbedit prog.bas          F2
    cc -r prog.bas
    ...
    hello from prog

Two runs.  The first opens an empty file, types a program and presses
F1 (save and exit); the file must hold the line.  The second opens the
saved file and presses F2 (save, exit, compile and run): the editor
execs the compiler named by MMBEDIT_CC with -r and the file, and the
program's output must appear on the same terminal.  F1 and F2 go as
the sequences MMBasic's MMInkey takes, ESC O P and ESC O Q, which is
what a real terminal sends for those keys.

    mmbedit_pty.py <mmbedit> <cc> <work dir>
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
    mmbedit, cc, work = sys.argv[1:4]
    os.makedirs(work, exist_ok=True)
    prog = os.path.join(work, "hello.bas")
    env = dict(os.environ, TERM="vt100", MMBEDIT_CC=cc, PC3_DISPLAY="off")
    fails = 0

    def check(what, ok):
        nonlocal fails
        print(("pass  " if ok else "FAIL  ") + what)
        if not ok:
            fails += 1

    # ---- F1: type a line, save and exit -------------------------------------
    open(prog, "w").close()
    p, fd = spawn([mmbedit, prog], env, work)
    out, drawn = read_until(fd, p, LEGEND, 5)
    check("the editor drew its screen", drawn)
    # a comment first: on a PC the editor paints it BRIGHT yellow
    # (SGR 93), the colour a PicoMite's TeraTerm shows for MMBasic's
    # SGR 33, which a PC terminal would paint brown
    os.write(fd, b"' a comment\r")
    time.sleep(0.3)
    # two stray characters, then the two deleting keys as a PC terminal
    # sends them: Backspace is 0x7F and must delete BACKWARDS (the Y),
    # Delete is ESC [ 3 ~ and deletes forwards (the X, after a Left)
    os.write(fd, b'Print "hello from mmbedit"XY')
    time.sleep(0.3)
    os.write(fd, b"\x7f")
    time.sleep(0.2)
    os.write(fd, b"\x1b[D")
    time.sleep(0.2)
    os.write(fd, b"\x1b[3~")
    time.sleep(0.3)
    os.write(fd, F1)
    out, _ = read_until(fd, p, None, 5)
    rc = p.wait(timeout=5)
    os.close(fd)
    check("F1 left the editor cleanly", rc == 0)
    text = open(prog, "rb").read()
    check("the file holds the typed line", b'Print "hello from mmbedit"' in text)
    last = text.replace(b"\r\n", b"\n").rstrip(b"\n").split(b"\n")[-1]
    check("Backspace deleted backwards and Delete forwards",
          last == b'Print "hello from mmbedit"')
    check("the comment was painted bright yellow", b"\x1b[93m" in out)
    check("no dim colours were emitted", b"\x1b[33m" not in out and b"\x1b[37m" not in out)

    # ---- F2: save, exit, compile and run -------------------------------------
    p, fd = spawn([mmbedit, prog], env, work)
    out, drawn = read_until(fd, p, LEGEND, 5)
    check("the editor drew its screen again", drawn)
    os.write(fd, F2)
    # The line as the PROGRAM prints it starts a line of its own; the
    # editor's copy of the source shows it inside Print "...".  The tty
    # turns the runtime's \r\n into \r\r\n, so match up to the text.
    out, ran = read_until(fd, p, b"\nhello from mmbedit", 60)
    rc = p.wait(timeout=10)
    os.close(fd)
    announced = out.find(b"cc -r ")
    check("F2 announced the compiler", announced >= 0)
    check("the program ran and printed on the terminal",
          ran and out.find(b"\nhello from mmbedit") > announced)
    check("the compile-and-run exited 0", rc == 0)
    # ---- a file that cannot be saved where it is -----------------------------
    # The installed examples belong to root.  The editor says so on its
    # status line when it opens one, and says WHY when F1 fails.
    ro = os.path.join(work, "ro")
    os.makedirs(ro, exist_ok=True)
    rofile = os.path.join(ro, "sample.bas")
    with open(rofile, "w") as f:
        f.write("Print 1\n")
    os.chmod(ro, 0o555)
    os.chmod(rofile, 0o444)
    if os.access(rofile, os.W_OK):
        print("skip  read-only checks (running as root)")
    else:
        p, fd = spawn([mmbedit, rofile], env, ro)
        out, drawn = read_until(fd, p, b"READ ONLY", 5)
        check("a read-only file is announced on the status line", drawn)
        os.write(fd, b"' more")
        time.sleep(0.3)
        os.write(fd, F1)
        out, said = read_until(fd, p, b"CANNOT SAVE: Permission denied", 5)
        check("F1 says why it cannot save", said)
        os.write(fd, b"\x1b")          # ESC: abandon
        time.sleep(0.3)
        os.write(fd, b"\x1b")          # and again, if it asked
        read_until(fd, p, None, 3)
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
        os.close(fd)
    os.chmod(ro, 0o755)
    os.chmod(rofile, 0o644)

    # ---- a paste: keys arriving during a redraw are kept -------------------
    # The editor used to drop every key that arrived while it redrew the
    # screen, to stop a held key running on; a paste lost everything after
    # its first line or so.  Now they are dropped only when every one is a
    # repeat of the key being handled (Editor.c's EdDropHeldRepeats, #51).
    # 60 lines in one write is past the 40-row screen, so it scrolls too.
    paste = os.path.join(work, "paste.bas")
    open(paste, "w").close()
    p, fd = spawn([mmbedit, paste], env, work)
    read_until(fd, p, LEGEND, 5)
    os.write(fd, b"".join(b"Print %d\r" % i for i in range(1, 61)))
    time.sleep(2)
    os.write(fd, F1)
    read_until(fd, p, None, 5)
    p.wait(timeout=5)
    os.close(fd)
    lines = open(paste, "rb").read().decode().split("\n")
    check("a 60-line paste arrived whole",
          all(("Print %d" % i) in lines for i in range(1, 61)))

    # ---- the last line's ending is the file's, not a line of its own --------
    # Loaded, it was an empty line below the program, and what was typed
    # there was saved with no ending at all (#52).
    one = os.path.join(work, "one.bas")
    with open(one, "w") as f:
        f.write("Print 1\n")
    p, fd = spawn([mmbedit, one], env, work)
    read_until(fd, p, LEGEND, 5)
    os.write(fd, b"\x1b[B")             # Down: there is no line below now
    time.sleep(0.3)
    os.write(fd, b"X")
    time.sleep(0.3)
    os.write(fd, F1)
    read_until(fd, p, None, 5)
    p.wait(timeout=5)
    os.close(fd)
    text = open(one, "rb").read()
    check("one line stays one line, ended", text.endswith(b"\n") and text.count(b"\n") == 1)

    # ---- Esc leaves even with a line too long to save ------------------------
    # The long-line refusal is for saving; Esc saves nothing, so it must not
    # be refused (#50).  The file is unchanged, so Esc asks nothing.
    longf = os.path.join(work, "long.bas")
    with open(longf, "w") as f:
        f.write("' " + "x" * 300 + "\n")
    p, fd = spawn([mmbedit, longf], env, work)
    read_until(fd, p, LEGEND, 5)
    os.write(fd, b"\x1b")
    out, _ = read_until(fd, p, None, 3)
    try:
        rc = p.wait(timeout=3)
    except subprocess.TimeoutExpired:
        p.kill()
        rc = None
    os.close(fd)
    check("Esc leaves a file with a line over 255", rc == 0 and b"TOO LONG" not in out)

    if fails:
        sys.stdout.write(out[-600:].decode("latin-1"))
    print("mmbedit_pty: " + ("FAILED" if fails else "all passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
