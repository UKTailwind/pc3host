' The same server, without its certificate: the machine's own roots do
' not vouch for a self-signed certificate, so the handshake must FAIL
' with the reference's message - which is what proves the session is
' TLS with verification and not plain TCP wearing the name.
Option EXPLICIT
Const cr = Chr$(13) + Chr$(10)
Dim Integer b(1024)
ON ERROR SKIP 1
WEB OPEN TLS CLIENT "127.0.0.1", 8443, 5000
Print "open: "; MM.ERRMSG$
