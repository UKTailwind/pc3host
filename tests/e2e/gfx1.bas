' gfx1.bas - MODE 2 through the whole chain: mmbc, cc, bcrun, the
' client library, the server, the kernel's display core.
'
' Draws a screen that exercises the primitives the server answers -
' rectangles, filled and outlined, a circle, lines, text, single pixels,
' a batch - and writes what PIXEL() reads back to a file, because PRINT
' in a graphics mode draws on the screen rather than the console.  The
' picture itself is taken afterwards with saveimage and compared with
' the golden.
MODE 2
CLS RGB(BLACK)
BOX 10, 10, 100, 60, 1, RGB(RED), RGB(BLUE)
BOX 120, 10, 60, 60, 3, RGB(GREEN)
CIRCLE 250, 60, 40, 2, 1, RGB(YELLOW), RGB(MAGENTA)
LINE 0, 239, 319, 100, 1, RGB(WHITE)
LINE 0, 100, 319, 239, 1, RGB(CYAN)
TEXT 160, 150, "PC3 ON A PC", "CM", 1, 1, RGB(WHITE), RGB(BLACK)
TEXT 160, 175, "mode 2", "CM", 1, 2, RGB(GREEN), -1
PIXEL 5, 5, RGB(MAGENTA)
PIXEL 6, 5, RGB(RED)
DIM x%(3) = (300, 301, 302, 303)
DIM y%(3) = (230, 231, 232, 233)
DIM c%(3) = (RGB(RED), RGB(GREEN), RGB(BLUE), RGB(WHITE))
PIXEL x%(), y%(), c%()
OPEN "gfx1.out" FOR OUTPUT AS #1
PRINT #1, PIXEL(5, 5)
PRINT #1, PIXEL(6, 5)
PRINT #1, PIXEL(50, 30)
PRINT #1, PIXEL(300, 230), PIXEL(301, 231), PIXEL(302, 232), PIXEL(303, 233)
PRINT #1, PIXEL(250, 60)
PRINT #1, PIXEL(0, 0)
PRINT #1, MM.HRES, MM.VRES
CLOSE #1
END
