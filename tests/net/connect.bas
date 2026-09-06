' WEB CONNECT and MM.INFO(IP ADDRESS): the server answers NETIOC_STATUS
' with the machine's own network, so a connected PC says "link up" and
' has an address that is not 0.0.0.0.
WEB CONNECT
Print "link up"
If MM.Info(IP ADDRESS) = "0.0.0.0" Then Print "no address" Else Print "has an address"
