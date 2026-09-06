' TLS to a local server whose certificate is the one WEB TLS CA loads:
' the handshake must succeed and the page arrive.  The address is
' dotted, so no host name is set and the chain alone is checked - as
' the board's kernel checks when it is given no name.
Option EXPLICIT
Const cr = Chr$(13) + Chr$(10)
Dim Integer b(1024)
WEB TLS CA "cert.pem"
WEB OPEN TLS CLIENT "127.0.0.1", 8443, 5000
WEB TCP CLIENT REQUEST "GET /hello.txt HTTP/1.0" + cr + cr, b()
Print "got "; LLen(b()); " bytes"
Print "status: "; LGetStr$(b(), 1, 15)
If LInStr(b(), "squirrels") > 0 Then Print "body ok" Else Print "BODY MISSING"
WEB CLOSE TCP CLIENT
