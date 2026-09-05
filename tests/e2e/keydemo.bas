' keydemo.bas - the keyboard, on the screen.
'
' Click the PC3 window to give it the keyboard, then type: each key's
' INKEY$ code appears, and the panel below shows KEYDOWN - how many keys
' are held at this instant, which, and the modifier and lock bits - so a
' held key, shift, caps lock and the arrows can all be watched.  Keys
' typed into the terminal you started from count too.  ESC quits.
'
' KEYDOWN empties the input queue before it answers, on a PicoMite and
' here alike, so this asks INKEY$ until the queue is empty and only then
' asks KEYDOWN - otherwise keys typed in a burst would vanish into it.
MODE 2
CLS
TEXT 160, 8, "PC3 KEYBOARD", "CT", 1, 1, RGB(YELLOW)
TEXT 160, 24, "click the window, then type; ESC quits", "CT", 1, 1, RGB(WHITE)
BOX 0, 40, 320, 100, 1, RGB(BLUE)
BOX 0, 150, 320, 60, 1, RGB(GREEN)
TEXT 4, 44, "INKEY$", "LT", 1, 1, RGB(CYAN)
TEXT 4, 154, "KEYDOWN", "LT", 1, 1, RGB(CYAN)
DIM line$ = ""
DIM last$ = ""
DIM shown$ = ""
DIM held$ = ""
DO
  k$ = INKEY$
  DO WHILE k$ <> ""
    IF ASC(k$) = 27 THEN
      CLS
      END
    ENDIF
    IF ASC(k$) >= 32 AND ASC(k$) < 127 THEN
      line$ = line$ + k$
    ELSE
      line$ = line$ + "<" + STR$(ASC(k$)) + ">"
    ENDIF
    IF LEN(line$) > 34 THEN line$ = RIGHT$(line$, 34)
    last$ = "last code " + STR$(ASC(k$))
    k$ = INKEY$
  LOOP
  IF line$ <> shown$ THEN
    BOX 2, 60, 316, 78, 0, RGB(BLACK), RGB(BLACK)
    TEXT 8, 64, line$, "LT", 1, 1, RGB(WHITE)
    TEXT 8, 100, last$, "LT", 1, 1, RGB(MAGENTA)
    shown$ = line$
  ENDIF
  h$ = "held " + STR$(KEYDOWN(0)) + "   codes " + STR$(KEYDOWN(1)) + " " + STR$(KEYDOWN(2)) + " " + STR$(KEYDOWN(3)) + "|modifiers " + STR$(KEYDOWN(7)) + "   locks " + STR$(KEYDOWN(8))
  IF h$ <> held$ THEN
    BOX 2, 170, 316, 38, 0, RGB(BLACK), RGB(BLACK)
    TEXT 8, 172, LEFT$(h$, INSTR(h$, "|") - 1), "LT", 1, 1, RGB(WHITE)
    TEXT 8, 190, MID$(h$, INSTR(h$, "|") + 1), "LT", 1, 1, RGB(WHITE)
    held$ = h$
  ENDIF
  PAUSE 10
LOOP
