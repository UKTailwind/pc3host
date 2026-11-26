' Serial I/O on a PC: OPEN "port:baud,buffer,interrupt,level" AS #n, the
' port named as the OS names it.  Here it is one end of a pseudo-terminal
' that serialpty.py has linked as ./ttyTEST and answers at the other end:
' a line for LINE INPUT, three bytes for LOC and INPUT$, and twelve for
' the interrupt, whose level is 12.  What the program saw goes to
' serial.out.
Dim got$, a$, t
Open "serial.out" For Output As #2
Open "./ttyTEST:115200,64,rx,12" As #1
Print #2, "lof"; Lof(#1); " loc"; Loc(#1); " eof"; Eof(#1)
Print #1, "ready"
Line Input #1, a$
Print #2, "line [" + a$ + "]"
Print #1, "next"
t = Timer
Do While Loc(#1) < 3 And Timer - t < 5000 : Pause 5 : Loop
Print #2, "loc"; Loc(#1); " [" + Input$(10, #1) + "]"; Loc(#1); Eof(#1)
Print #1, "int"
t = Timer
Do While got$ = "" And Timer - t < 5000 : Pause 5 : Loop
Print #2, "interrupt [" + got$ + "]"
Print #1, "bye"
Close #1
Print #2, "closed"
Close #2
End

Sub rx
  got$ = Input$(12, #1)
End Sub
