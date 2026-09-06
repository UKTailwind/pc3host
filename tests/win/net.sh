#!/bin/bash
#
# The WEB family on Windows: tests/net/run.sh's shape, over Winsock.
#
#   bash tests/win/net.sh <tree> <pc3host> <fuzix> <work>
#
# from Git Bash.  Loopback only, except one real fetch: a TLS session to
# example.com, which is what proves the certificate store is being read
# (Windows keeps its roots there, not in a file).  Needs curl and
# python, both of which Windows 10 and later have.
T=$1; P=$2; FZ=$3; W=$4
[ -x "$T/bin/bcrun.exe" ] && [ -d "$FZ/Applications/mmb2c" ] && [ -n "$W" ] || {
	echo "usage: net.sh <tree> <pc3host> <fuzix> <work>" >&2; exit 2; }
M=$FZ/Applications/mmb2c
export PC3_SOCKET="$(cygpath -w "$TEMP")\\pc3-net.sock"
export PC3_AUTOSTART=0
rm -f "$TEMP/pc3-net.sock"
rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
"$T/bin/pc3d.exe" --headless --audio null --socket "$PC3_SOCKET" > pc3d.log 2>&1 &
SRV=$!
sleep 1.5
fail=0
build() {			# build <name> <source>
	cp "$2" "$1.bas" 2>/dev/null || return 1
	"$T/bin/cc.exe" "$1.bas" > "$1.build.log" 2>&1 || {
		echo "FAIL  $1 (build)"; tail -3 "$1.build.log"; fail=1; return 1; }
	return 0
}
expect() {			# expect <name> <file> <text>
	if grep -q -- "$3" "$2"; then echo "pass  $1"
	else echo "FAIL  $1: no '$3' in $2"; tr -d '\r' < "$2" | head -5; fail=1; fi
}
waitport() {			# waitport <port>
	for i in $(seq 1 50); do
		curl -s -m 1 -o /dev/null "http://127.0.0.1:$1/" && return 0
		curl -s -m 1 -o /dev/null "http://127.0.0.1:$1/" 2>/dev/null
		[ $? -le 60 ] && netstat -an | grep -q "127.0.0.1:$1 .*LISTENING" && return 0
		sleep 0.2
	done
	return 1
}

echo "hello squirrels" > hello.txt
if build websrv "$M/samples/websrv.bas"; then
	timeout 60 "$T/bin/bcrun.exe" websrv.bc > websrv.out 2>&1 &
	WS=$!
	waitport 8080 || echo "FAIL  websrv did not listen"
	curl -s -m 5 http://127.0.0.1:8080/hello.txt > websrv.get 2>&1
	code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/)
	kill $WS 2>/dev/null; wait $WS 2>/dev/null
	expect "websrv: file served" websrv.get "squirrels"
	[ "$code" = 204 ] && echo "pass  websrv: / answers 204" || { echo "FAIL  websrv: / answered $code"; fail=1; }
fi

if build webget "$M/samples/webget.bas"; then
	python -m http.server 8080 --bind 127.0.0.1 > httpd.log 2>&1 &
	HTTPD=$!
	waitport 8080 || echo "FAIL  httpd did not come up"
	timeout 30 "$T/bin/bcrun.exe" webget.bc 127.0.0.1 > webget.out 2>&1
	expect "webget: body" webget.out "body ok"
	kill $HTTPD 2>/dev/null; wait $HTTPD 2>/dev/null
fi

if [ -f websrv.bc ]; then
	timeout 20 "$T/bin/bcrun.exe" websrv.bc > websrv2.out 2>&1 &
	WS=$!
	if waitport 8080; then
		code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/)
		[ "$code" = 204 ] && echo "pass  websrv: rebinds a port in TIME_WAIT" || { echo "FAIL  websrv: rebind answered $code"; fail=1; }
	else
		echo "FAIL  websrv: could not rebind 8080"; tr -d '\r' < websrv2.out | head -3; fail=1
	fi
	kill $WS 2>/dev/null; wait $WS 2>/dev/null
fi

if build udprecv "$M/samples/udprecv.bas" && build udpsend "$M/samples/udpsend.bas"; then
	timeout 40 "$T/bin/bcrun.exe" udprecv.bc > udprecv.out 2>&1 &
	UR=$!
	sleep 1
	timeout 30 "$T/bin/bcrun.exe" udpsend.bc 127.0.0.1 > udpsend.out 2>&1
	wait $UR 2>/dev/null
	expect "udp: five acks back" udpsend.out "acks:  5"
	expect "udp: five received" udprecv.out "done:  5"
fi

if build connect "$P/tests/net/connect.bas"; then
	timeout 30 "$T/bin/bcrun.exe" connect.bc > connect.out 2>&1
	expect "connect: link up" connect.out "link up"
	expect "connect: address" connect.out "has an address"
fi

# The one that leaves the machine: TLS to example.com, verified against
# the roots in the Windows certificate store (client/pc3tls.c reads them
# there, where a Linux host reads /etc/ssl/certs).  A machine with no
# route to the internet cannot run this one, and says so rather than
# failing.
if curl -s -m 5 -o /dev/null https://example.com; then
	cat > tls_live.bas <<'BASEOF'
Option EXPLICIT
Const cr = Chr$(13) + Chr$(10)
Dim Integer b(1024)
WEB OPEN TLS CLIENT "example.com", 443, 20000
WEB TCP CLIENT REQUEST "GET / HTTP/1.0" + cr + "Host: example.com" + cr + cr, b()
Print "status: "; LGetStr$(b(), 1, 15)
WEB CLOSE TCP CLIENT
BASEOF
	if "$T/bin/cc.exe" tls_live.bas > tls_live.build.log 2>&1; then
		timeout 30 "$T/bin/bcrun.exe" tls_live.bc > tls_live.out 2>&1
		expect "tls: 200 from example.com through the certificate store" tls_live.out "status: HTTP/1.1 200"
	else
		echo "FAIL  tls_live (build)"; tail -3 tls_live.build.log; fail=1
	fi
else
	echo "skip  tls: no route to the internet"
fi

kill $SRV 2>/dev/null
exit $fail
