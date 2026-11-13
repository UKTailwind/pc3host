#!/usr/bin/env python3
"""PgUp and PgDn over whole and part screens, through a pseudo-terminal.

PgUp scrolled a full screen whenever the top line was at least one short
of a screen (edy >= VHeight - 1), so in a file one line short of a whole
number of screens, paged to the end and back, the top line went to -1: a
blank top row, and the next PgUp moved the cursor DOWN a line (PicoMite
d059604, FUZIX editor.c).  This pages to the end and back through files
around every screen boundary and reads the cursor's line from the status
line ("L:n") after each key: it must never go up while paging up, and it
must finish on line 1.

    mmbedit_pgup.py <mmbedit> <work dir>
"""
import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import termios
import time

PGUP = b"\x1b[5~"
PGDN = b"\x1b[6~"
LEGEND = b"F1:Save"
STATUS = re.compile(rb"L:(\d+) C:")


def spawn(argv, cwd):
    master, slave = os.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 80, 0, 0))

    def child():
        os.setsid()
        fcntl.ioctl(slave, termios.TIOCSCTTY, 0)

    env = dict(os.environ, TERM="xterm")
    p = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave,
                         preexec_fn=child, env=env, cwd=cwd, close_fds=True)
    os.close(slave)
    return p, master


def gather(fd, quiet=0.25, limit=5.0):
    """everything the editor writes until it has been quiet a while"""
    buf = b""
    end = time.time() + limit
    last = time.time()
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                d = os.read(fd, 65536)
            except OSError:
                break
            if not d:
                break
            buf += d
            last = time.time()
        elif buf and time.time() - last > quiet:
            break
    return buf


def line_now(out):
    m = STATUS.findall(out)
    return int(m[-1]) if m else None


class Editor:
    def __init__(self, mmbedit, work, nlines):
        self.path = os.path.join(work, "pg%d.bas" % nlines)
        with open(self.path, "w") as f:
            for k in range(1, nlines + 1):
                f.write("' line %d\n" % k)
        self.p, self.fd = spawn([mmbedit, self.path], work)
        out = gather(self.fd, quiet=0.5)
        if LEGEND not in out:
            raise SystemExit("mmbedit did not draw its screen for %d lines"
                             % nlines)
        self.line = line_now(out) or 1

    def key(self, k):
        os.write(self.fd, k)
        n = line_now(gather(self.fd))
        if n is not None:
            self.line = n
        return self.line

    def close(self):
        self.p.kill()
        self.p.wait()
        os.close(self.fd)
        os.unlink(self.path)


def screen_height(mmbedit, work):
    """VHeight, from how far one PgDn moves the cursor in a long file"""
    e = Editor(mmbedit, work, 400)
    try:
        return e.key(PGDN) - 1
    finally:
        e.close()


def check(mmbedit, work, nlines, vh):
    """page to the end and back: return a complaint, or None"""
    e = Editor(mmbedit, work, nlines)
    try:
        for _ in range(nlines // vh + 3):
            e.key(PGDN)
        seen = [e.line]
        for _ in range(nlines // vh + 3):
            seen.append(e.key(PGUP))
        for a, b in zip(seen, seen[1:]):
            if b > a:
                return "%d lines: PgUp moved the cursor down, L:%d -> L:%d " \
                       "(%s)" % (nlines, a, b, seen)
        if seen[-1] != 1:
            return "%d lines: PgUp to the top ends on L:%d, not 1 (%s)" % (
                nlines, seen[-1], seen)
        return None
    finally:
        e.close()


def main():
    mmbedit, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    vh = screen_height(mmbedit, work)
    if vh < 10:
        raise SystemExit("could not tell the screen height (PgDn moved %d)"
                         % vh)
    sizes = sorted({1, 2} | {k * vh + d for k in (1, 2, 3) for d in (-1, 0, 1)})
    bad = [c for c in (check(mmbedit, work, n, vh) for n in sizes) if c]
    for c in bad:
        print("FAIL " + c)
    print("mmbedit PgUp/PgDn: %d file sizes around a %d-line screen, %d bad"
          % (len(sizes), vh, len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
