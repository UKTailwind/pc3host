"""The other end of serial.bas's port: a pseudo-terminal, its program's end
linked as <work>/ttyTEST, answering what the program sends.  Prints what
the program sent, in order.
    serialpty.py <work> <bcrun> <prog.bc>"""
import os
import select
import subprocess
import sys
import time
import tty

work, bcrun, bc = sys.argv[1:4]
m, s = os.openpty()
tty.setraw(s)
link = os.path.join(work, 'ttyTEST')
if os.path.lexists(link):
    os.unlink(link)
os.symlink(os.ttyname(s), link)
log = open(os.path.join(work, 'serial.stdout'), 'wb')
proc = subprocess.Popen([bcrun, bc], cwd=work, stdin=subprocess.DEVNULL,
                        stdout=log, stderr=subprocess.STDOUT)
buf = b''


def expect(word, timeout=10.0):
    global buf
    end = time.time() + timeout
    while word not in buf:
        if time.time() > end or proc.poll() is not None:
            proc.kill()
            sys.exit('serialpty: no %r from the program (had %r)' % (word, buf))
        r, _, _ = select.select([m], [], [], 0.05)
        if r:
            buf += os.read(m, 256)
    k = buf.index(word) + len(word)
    got, buf = buf[:k], buf[k:]
    print(repr(got))


expect(b'ready\r\n')
os.write(m, b'line one\n')
expect(b'next\r\n')
os.write(m, b'XYZ')
expect(b'int\r\n')
os.write(m, b'123456789ABC')
expect(b'bye\r\n')
try:
    proc.wait(timeout=10)
except subprocess.TimeoutExpired:
    proc.kill()
    sys.exit('serialpty: the program did not end')
print('exit %d' % proc.returncode)
os.unlink(link)
