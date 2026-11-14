' OPTION PRINTSCREEN through the window's keyboard.  run.sh presses
' Print Screen once in each of four two-second phases, counted from
' when ref.bmp appears.  Each press while the option is on must save
' the screen as SCREEN_*.BMP - .PNG after FORMAT PNG - the same picture
' as ref.bmp, and the program must never read the key: in a loop that
' reads no keys (1), under INKEY$ (2) and in a PAUSE (3).  With the
' option off (4) the key is a key again, 157.
MODE 2
CLS RGB(BLUE)
CIRCLE 160, 120, 80, , , RGB(YELLOW), RGB(RED)
OPTION PRINTSCREEN ON
OPEN "prtscr.out" FOR OUTPUT AS #1
SAVE IMAGE "ref.bmp"
t = TIMER
DO WHILE TIMER < t + 2000
  n = n + 1
LOOP
DO WHILE TIMER < t + 4000
  k$ = INKEY$
  IF k$ <> "" THEN PRINT #1, "key"; ASC(k$)
LOOP
OPTION PRINTSCREEN FORMAT PNG
PAUSE t + 6000 - TIMER
OPTION PRINTSCREEN OFF
DO WHILE TIMER < t + 8000
  k$ = INKEY$
  IF k$ <> "" THEN PRINT #1, "key"; ASC(k$)
LOOP
PRINT #1, "end"
CLOSE #1
