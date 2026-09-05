' keys.bas - the window's keyboard through INKEY$ and KEYDOWN.
'
' Runs with no terminal (stdin is /dev/null in the test), so every key
' it sees came from the server: pc3key presses them.  Each INKEY$ code
' goes to the file; when the key is "a" - which the script holds down
' for a while - KEYDOWN is read too, so the held-key table is checked
' while the key is still down; "q" ends it.
MODE 2
OPEN "keys.out" FOR OUTPUT AS #1
DO
  k$ = INKEY$
  IF k$ <> "" THEN
    PRINT #1, ASC(k$)
    IF k$ = "a" THEN PRINT #1, "held", KEYDOWN(0), KEYDOWN(1)
    IF k$ = "q" THEN EXIT DO
  ENDIF
LOOP
CLOSE #1
END
