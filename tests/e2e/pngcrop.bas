' LOAD PNG crops a picture bigger than the screen and takes the image's
' offsets; SPRITE LOADPNG takes a part of it (PicoMite 4d91e6c).  Either
' may leave an argument blank.  big.png is 400 by 300, its pixel x, y in
' C((x \ 8 + (y \ 8) * 3) Mod 15); it used to be "Image too large".
MODE 2
Dim integer C(15), n
For n = 0 To 15 : Read C(n) : Next

' image column 50, row 30 at 10, 20; the background is C(15)
CLS C(15)
LOAD PNG "big", 10, 20, , , 50, 30
Print "offsets:"; Check(10, 20, 50, 30)
' no position: the top left at 0, 0
CLS C(15)
LOAD PNG "big.png"
Print "whole:"; Check(0, 0, 0, 0)
' a negative position cuts the image's top and left
CLS C(15)
LOAD PNG "big", -100, -50
Print "negative:"; Check(-100, -50, 0, 0)
' an offset outside the image
On Error Skip
LOAD PNG "big", 0, 0, , , 400, 0
Print MM.ErrMsg$

' a 40 by 24 part from 16, 8, as a sprite, written at 100, 100
SPRITE LOADPNG 1, "big", , , 16, 8, 40, 24
Print "sprite:"; Sprite(W, 1); Sprite(H, 1);
CLS C(15)
SPRITE WRITE 1, 100, 100, 0
Dim integer bad, x, y
For y = 0 To 23
  For x = 0 To 39
    If Pixel(100 + x, 100 + y) <> C(((16 + x) \ 8 + ((8 + y) \ 8) * 3) Mod 15) Then bad = bad + 1
  Next
Next
Print bad
On Error Skip
SPRITE LOADPNG 2, "big", , , 390, 0, 20
Print MM.ErrMsg$

' every third row and fifth column of the screen against the picture,
' image pixel 0, 0 being at ex, ey and the first shown column, row xo, yo
Function Check(px, py, xo, yo) As integer
  Local integer x, y, w, ix, iy
  For y = 0 To MM.VRes - 1 Step 3
    For x = 0 To MM.HRes - 1 Step 5
      ix = x - px + xo : iy = y - py + yo
      w = C(15)
      If x >= px And y >= py And ix < 400 And iy < 300 Then w = C((ix \ 8 + (iy \ 8) * 3) Mod 15)
      If Pixel(x, y) <> w Then Check = Check + 1
    Next
  Next
End Function

Data &H000000, &H0000FF, &H004000, &H0040FF, &H008000, &H0080FF, &H00FF00, &H00FFFF
Data &HFF0000, &HFF00FF, &HFF4000, &HFF40FF, &HFF8000, &HFF80FF, &HFFFF00, &HFFFFFF
