' WEB TLS CA "/etc/ca.pem" is what a program written for the board says.
' A PC has no /etc/ca.pem; bcrun hands it the bundle the package ships,
' the board's own ten roots, so the statement succeeds - and the local
' server's self-signed certificate, not among them, is still refused.
Option EXPLICIT
Dim Integer b(1024)
WEB TLS CA "/etc/ca.pem"
Print "ca loaded"
ON ERROR SKIP 1
WEB OPEN TLS CLIENT "127.0.0.1", 8443, 5000
Print "open: "; MM.ERRMSG$
WEB TLS NOVERIFY
WEB OPEN TLS CLIENT "127.0.0.1", 8443, 5000
Print "noverify: connected"
WEB CLOSE TCP CLIENT
