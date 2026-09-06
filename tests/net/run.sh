#!/bin/bash
#
# The network gate, on the loopback and nothing else:
#
#   webget.bas    WEB OPEN TCP CLIENT / REQUEST against a local HTTP server
#   websrv.bas    the BASIC web server, fetched with curl
#   udpsend/recv  the UDP pair from the tree's samples, talking to each other
#   connect.bas   WEB CONNECT and MM.INFO(IP ADDRESS) from NETIOC_STATUS
#   tls_ok.bas    WEB TLS CA + WEB OPEN TLS CLIENT against a local TLS
#                 server with a self-signed certificate: succeeds
#   tls_noca.bas  the same without the certificate: refused, with the
#                 reference's message
#
#   bash tests/net/run.sh <bin dir> <FUZIX root> <work dir>
#
# Names (webname.bas), WEB NTP and a real TLS site need the internet and
# are not gated; they were run by hand.

BIN=$1
FZ=$2
W=$3
D=$(cd "$(dirname "$0")" && pwd)
M=$FZ/Applications/mmb2c
export PC3_SOCKET=$W/net.sock
export PC3_AUTOSTART=0
unset PC3_DISPLAY

fail=0
pids=""
die() { echo "net: $*" >&2; exit 1; }
cleanup() { kill $pids $SRV 2>/dev/null; wait 2>/dev/null; }
trap cleanup EXIT

[ -x "$BIN/pc3d" ] || die "no $BIN/pc3d"
rm -rf "$W"
mkdir -p "$W" || die "cannot make $W"
cd "$W" || die "cannot enter $W"

"$BIN/pc3d" --headless --audio null --socket "$PC3_SOCKET" &
SRV=$!
for i in $(seq 1 50); do
	[ -S "$PC3_SOCKET" ] && break
	sleep 0.1
done
[ -S "$PC3_SOCKET" ] || die "server did not come up"

build() {			# build <name> <source>
	( BIN=$BIN W=$W MMB2C=$BIN/mmbc bash "$M/fcc/fccbuild.sh" "$2" ) > "$W/$1.build.log" 2>&1 \
		|| { echo "FAIL  $1 (build)"; tail -3 "$W/$1.build.log"; fail=1; return 1; }
	return 0
}
expect() {			# expect <name> <file> <text>
	if grep -q -- "$3" "$2"; then
		echo "pass  $1"
	else
		echo "FAIL  $1: no '$3' in $2"; tr -d '\r' < "$2" | head -5; fail=1
	fi
}
waitport() {			# waitport <port>
	for i in $(seq 1 50); do
		(echo > /dev/tcp/127.0.0.1/$1) 2>/dev/null && return 0
		sleep 0.1
	done
	return 1
}

echo "hello squirrels" > hello.txt

# ---- the BASIC web server, fetched with curl ------------------------------------
# First, before anything else has used port 8080: python's server sets
# SO_REUSEADDR and does not mind a port in TIME_WAIT; the BASIC one
# relies on bcrun's bind doing the same, which is also under test.
if build websrv "$M/samples/websrv.bas"; then
	timeout 60 "$BIN/bcrun" websrv.bc > websrv.out 2>&1 &
	WS=$!; pids="$pids $WS"
	waitport 8080 || echo "FAIL  websrv did not listen"
	curl -s -m 5 http://127.0.0.1:8080/hello.txt > websrv.get 2>&1
	code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/)
	kill -INT $WS 2>/dev/null; wait $WS 2>/dev/null
	expect "websrv: file served" websrv.get "squirrels"
	[ "$code" = 204 ] && echo "pass  websrv: / answers 204" || { echo "FAIL  websrv: / answered $code"; fail=1; }
fi

# ---- TCP client against python's HTTP server, on the port just vacated -----------
if build webget "$M/samples/webget.bas"; then
	python3 -m http.server 8080 --bind 127.0.0.1 > httpd.log 2>&1 &
	HTTPD=$!; pids="$pids $HTTPD"
	waitport 8080 || echo "FAIL  httpd did not come up"
	timeout 30 "$BIN/bcrun" webget.bc 127.0.0.1 > webget.out 2>&1
	expect "webget: body" webget.out "body ok"
	kill $HTTPD 2>/dev/null; wait $HTTPD 2>/dev/null
fi

# ---- a server again on the same port: TIME_WAIT must not refuse the bind ---------
if [ -f websrv.bc ]; then
	timeout 20 "$BIN/bcrun" websrv.bc > websrv2.out 2>&1 &
	WS=$!; pids="$pids $WS"
	if waitport 8080; then
		code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/)
		[ "$code" = 204 ] && echo "pass  websrv: rebinds a port in TIME_WAIT" || { echo "FAIL  websrv: rebind answered $code"; fail=1; }
	else
		echo "FAIL  websrv: could not rebind 8080"; tr -d '\r' < websrv2.out | head -3; fail=1
	fi
	kill -INT $WS 2>/dev/null; wait $WS 2>/dev/null
fi

# ---- the UDP pair ---------------------------------------------------------------
if build udprecv "$M/samples/udprecv.bas" && build udpsend "$M/samples/udpsend.bas"; then
	timeout 40 "$BIN/bcrun" udprecv.bc > udprecv.out 2>&1 &
	UR=$!; pids="$pids $UR"
	sleep 1
	timeout 30 "$BIN/bcrun" udpsend.bc 127.0.0.1 > udpsend.out 2>&1
	wait $UR 2>/dev/null
	expect "udp: five acks back" udpsend.out "acks:  5"
	expect "udp: five received" udprecv.out "done:  5"
fi

# ---- WEB CONNECT and MM.INFO(IP ADDRESS) -----------------------------------------
if build connect "$D/connect.bas"; then
	timeout 30 "$BIN/bcrun" connect.bc > connect.out 2>&1
	expect "connect: link up" connect.out "link up"
	expect "connect: address" connect.out "has an address"
fi

# ---- TLS against a local server with a self-signed certificate -------------------
openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 2 \
	-subj "/CN=localhost" -addext "subjectAltName=IP:127.0.0.1,DNS:localhost" > openssl.log 2>&1 \
	|| { echo "FAIL  openssl could not make a certificate"; fail=1; }
cat > tlsserver.py <<'EOF'
import http.server, ssl
srv = http.server.HTTPServer(("127.0.0.1", 8443), http.server.SimpleHTTPRequestHandler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain("cert.pem", "key.pem")
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
EOF
python3 tlsserver.py > tlsserver.log 2>&1 &
TLSD=$!; pids="$pids $TLSD"
waitport 8443 || echo "FAIL  tls server did not come up"
if build tls_ok "$D/tls_ok.bas"; then
	timeout 30 "$BIN/bcrun" tls_ok.bc > tls_ok.out 2>&1
	expect "tls: status 200 through the handshake" tls_ok.out "status: HTTP/1.0 200"
	expect "tls: body" tls_ok.out "body ok"
fi
if build tls_noca "$D/tls_noca.bas"; then
	timeout 30 "$BIN/bcrun" tls_noca.bc > tls_noca.out 2>&1
	expect "tls: refused without the certificate" tls_noca.out "open: No response from TLS server"
fi
if build tls_etcca "$D/tls_etcca.bas"; then
	timeout 30 "$BIN/bcrun" tls_etcca.bc > tls_etcca.out 2>&1
	expect "tls: /etc/ca.pem is the shipped bundle" tls_etcca.out "ca loaded"
	expect "tls: the shipped roots refuse a self-signed peer" tls_etcca.out "open: No response from TLS server"
	expect "tls: NOVERIFY connects anyway" tls_etcca.out "noverify: connected"
fi
kill $TLSD 2>/dev/null; wait $TLSD 2>/dev/null

exit $fail
