' writebuff.bas - MM.INFO(WRITEBUFF) is the framebuffer laid out as a
' PicoMite lays it out (RGB121.c DrawPixel16, Draw.c DrawPixel2).  MODE 2
' is 160 bytes a row, two RGB121 pixels a byte, the LEFT one in the low
' four bits; MODE 1 is 80 bytes a row, the leftmost pixel in bit 0.  What
' the primitives drew is PEEKed, POKEd pixels are read back with PIXEL(),
' and MEMORY COPY moves a row.  The answers are worked from those two
' functions:
'   18 F0              red 8 left of blue 1; black left of white 15
'   FF FF00            &H61 POKEd: blue 1 on the left, green 6 right
'   FF0000 FF FFFFFF   row 0 copied to row 2
'   1 2                MODE 1: x 0 is bit 0 of byte 0, x 9 bit 1 of byte 1
MODE 2
CLS RGB(BLACK)
a = MM.INFO(WRITEBUFF)
OPEN "writebuff.out" FOR OUTPUT AS #1
PIXEL 0, 0, RGB(RED)
PIXEL 1, 0, RGB(BLUE)
PIXEL 3, 0, RGB(WHITE)
PRINT #1, HEX$(PEEK(BYTE a)); " "; HEX$(PEEK(BYTE a + 1))
POKE BYTE a + 160, &H61
PRINT #1, HEX$(PIXEL(0, 1)); " "; HEX$(PIXEL(1, 1))
MEMORY COPY a, a + 320, 160
PRINT #1, HEX$(PIXEL(0, 2)); " "; HEX$(PIXEL(1, 2)); " "; HEX$(PIXEL(3, 2))
MODE 1
b = MM.INFO(WRITEBUFF)
PIXEL 0, 0, RGB(WHITE)
PIXEL 9, 0, RGB(WHITE)
PRINT #1, STR$(PEEK(BYTE b)); " "; STR$(PEEK(BYTE b + 1))
CLOSE #1
