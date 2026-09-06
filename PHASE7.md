# Phase 7: networking

What REVIEW.md's work plan asked of phase 7: BSD sockets behind the
`WEB` family, TLS through mbedtls, the library the kernel already uses;
gate, `retic.bas` and the `WEB` samples. Dated 2026-09-06. Windows is
deferred by decision: everything works under Linux first.

## What works

`WEB OPEN TCP CLIENT`, `WEB TCP CLIENT REQUEST`, `WEB TCP SERVER` with
its interrupt and `WEB TRANSMIT`, `WEB UDP SERVER`/`SEND` with
`MM.ADDRESS$` and `MM.MESSAGE$`, `WEB CONNECT` and `MM.INFO(IP
ADDRESS)`, `WEB PING`, `WEB NTP`, and `WEB OPEN TLS CLIENT` with `WEB
TLS CA` and `WEB TLS NOVERIFY`, against real hosts as well as the
loopback: a name resolves, `www.google.com` answers over TLS with the
machine's own roots, `pool.ntp.org` answers, and the tree's `stage7`
smoke test runs through `CONNECT`, `PING` and `NTP` to its last line.

## What was already there, and what was wrong with it

The WEB family lives program-side in `mmb_net.h`, `mmb_webc.h`,
`mmb_webs.h` and `mmb_udp.h`, over nine socket libcalls in `bcrun`, and
the hosted `bcrun` already translated the Fuzix socket ABI to Linux's
for the gates. So TCP and UDP on the loopback worked before this phase
began. Four things did not, and each was found by running a sample:

**Names never resolved.** The header's own resolver reads
`/etc/resolv.conf` for a `nameserver` line - and read the first 256
bytes of it. On a systemd machine the file is the resolver stub's, and
its `nameserver 127.0.0.53` sits under 700 bytes of comment. Every name
failed with "Failed to find TCP address"; on the user's Ubuntu PC it
would have too. The header now reads the file in pieces, carrying an
unfinished line across. A board fix as much as a host one, though the
board's file is a line or two.

**TLS was plaintext.** The hosted `bcrun` mapped `IPPROTO_TLS` to plain
TCP and answered `SIOCTLSHOST` with a silent yes - right for gates that
never connect, and exactly the divergence that outranks a missing
feature once a program does. Now real, below.

**A remote connect looked refused.** A program's connect loop calls
`connect` until it answers 0. Fuzix answers 0 once a non-blocking
connect has completed; Linux answers `EISCONN`, which the loop took for
a refusal. Loopback connects complete on the first call and hid it.
The hosted `bcrun` maps `EISCONN` to 0.

**A server could not restart for a minute.** Its port was in
`TIME_WAIT` and the bind set no `SO_REUSEADDR`. The hosted `bcrun` sets
it, as the board's stack does not hold a closed port against the next
listener.

Two more were behaviours of Linux tools where the header expected
Fuzix's: `ping` exits 1 for "no replies" where the board's exits 0, so
an unanswered `WEB PING` raised - the hosted runtime treats that exit
of that program as the board does; and `ntpdate` may not exist and
must not set a PC's clock - `tools/ntpdate.c` asks the server the SNTP
question and exits 0 if it answered, and the clock stays the operating
system's. `bcrun` puts its own directory first on a program's PATH, so
that is the `ntpdate` a program runs.

## TLS: the board's socket, in the hosted bcrun

On the board `socket(AF_INET, SOCK_STREAM, IPPROTO_TLS)`, `ioctl(fd,
SIOCTLSHOST, name)`, `connect`, `read`, `write`, `close` - the kernel
does the rest with mbedtls. On a PC `client/pc3tls.c` does the same
with the same library: `ext/mbedtls` is a submodule pinned at 3.6.6,
the version the kernel's SDK carries, built as one static archive from
its sources so `bcrun` stays a static program (447K to 1.4M). `bcrun`
keeps a table of descriptors that were asked for as TLS and routes
their `connect`, `read`, `write`, `close` and the two ioctls there; the
descriptor the program holds is the TCP socket, so `fcntl` and the
non-blocking flag work on it unchanged.

The handshake is driven from `connect`: the program's loop keeps
calling until it answers 0, and a TLS connect answers `EINPROGRESS`
until the TCP connection is up and the handshake has finished, so the
same loop and the same timeout cover both, and a handshake that fails
answers `ECONNREFUSED`, which the program reports as "No response from
TLS server (handshake timeout)" - the board's message for any handshake
failure. mbedtls's own socket BIO turns `EAGAIN` into `WANT_READ`, and
that comes back out as `EAGAIN`, so a program polling a non-blocking
TLS socket sees what it sees from a TCP one.

**Roots.** The machine's bundle (`/etc/ssl/certs/ca-certificates.crt`)
is loaded at first use when it exists, so a hosted TLS session is
authenticated by default where the board's is encrypted-only until
`WEB TLS CA`. `WEB TLS CA file$` replaces it; `WEB TLS NOVERIFY` drops
verification, the kernel's meaning of a zero-length bundle. A name given
through `SIOCTLSHOST` is checked against the certificate; a connection
by dotted address checks the chain alone, which is what the kernel
does, and mbedtls 3.6 wants told so explicitly. `PC3_TLS_DEBUG=1`
prints what mbedtls said when a handshake fails, since the program only
learns "timeout".

**`/etc/ca.pem`.** Programs written for the board name the board's
bundle literally - `retic.bas`, the `webtls` sample, the manual. A PC
has no such file, and the header's buffer holds 20K, not the system's
200K bundle. So the package ships the board's `ca.pem` (ten roots, 12K)
under `share/pc3host`, and `bcrun`'s `open` hands it to a program
asking for `/etc/ca.pem` when there is none - the same roots the
program would have trusted on the board. A real `/etc/ca.pem` wins.
The tree's `webtls` sample, unchanged, fetches www.google.com this way.
`WEB TLS CA` is issued through `/dev/sys`, as on the board, so like
`MM.INFO(IP ADDRESS)` it wants the server up - which a desktop's
autostart gives it, and a script with `PC3_DISPLAY=off` does not.

## The server's part

`NETIOC_STATUS` is what `WEB CONNECT` and `MM.INFO(IP ADDRESS)` ask,
and the server answers it from the machine's own network
(`server/netinfo.c`): the interface the default route leaves by, its
address and mask, the gateway, the hardware address, the nameservers.
No default route, no link, which is honest for an offline PC.
`NETIOC_UP` and `DOWN` are a yes; `NETIOC_TLSCA` never reaches the
server from `bcrun`, whose TLS layer takes it in the same process as
the socket.

## Gates

`net` (`tests/net/run.sh`), on the loopback and nothing else: the BASIC
web server fetched with `curl` (a file served, `/` answering 204, and a
rebind of its port in `TIME_WAIT`); the TCP client against Python's
HTTP server; the UDP pair from the samples talking to each other; `WEB
CONNECT` and an address that is not 0.0.0.0; and TLS against a local
server with a self-signed certificate made by `openssl` - the handshake
succeeds with the certificate given to `WEB TLS CA`, is refused with the
machine's roots, is refused with the shipped `/etc/ca.pem`, and connects
under `NOVERIFY`. Refusal is what proves the session is TLS with
verification and not TCP wearing the name.

Run by hand, needing the internet: `webname.bas` (example.com, 200 OK),
a TLS GET from www.google.com with the system roots (200 OK), the
`ntpdate` shim against pool.ntp.org, and `stage7.bas` end to end.

The corpus gate found one arrival: `webservz`'s golden says `ip:
0.0.0.0`, blessed with no `/dev/sys`, and the server now answers with
the machine's address. A limitation of the file, listed with that
reason in `tests/sweep/corpus.known`, which is fifteen names now.

`retic.bas`, the plan's acceptance application, compiles through `cc`
with the tree's three known warnings and runs until its first `SETPIN`,
which a PC refuses until phase 8; its network half is the same `WEB`
statements the gate exercises.

## Board objects

`bcrun.o`, `ccbc.o` and `mmedit.o` for the board are byte-identical
before and after: every `bcrun` and runtime change is under `PC3_HOST`
or `__linux__`. The resolver fix is in a program-side header and reaches
the board's programs when they are next compiled there.

## Not in phase 7

* MQTT, which PLAN-web.md designed and did not build.
* Email: `gmail.bas` wants `/etc/gmail.conf` and an account; the path
  it uses (`WEB OPEN TLS CLIENT "smtp.gmail.com", 465`) is the TLS path
  above.
* `WEB NTP` does not set the clock, by decision; a PC's is its own.
* Windows: sockets, the spawn seam and the console are section 4.6's
  list, deferred until everything works under Linux.
