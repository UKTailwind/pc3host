' SPRITE(B, n [, side]): a sprite's opaque pixels against the background
' stored under it - 0 none of it opaque, 1 some, 2 some inside the
' sprite's shape - and the eight sides of the last answer, as the
' PicoMite's fun_sprite.  It was not translated.  The screen is black,
' the transparent colour, with single white pixels for background.
MODE 2
Dim integer a(63), i, s
CLS 0

' issue #92's case: a 5 x 4 sprite over one white pixel at its (0, 2)
For i = 0 To 19 : a(i) = RGB(255, 0, 0) : Next
SPRITE LOADARRAY #1, 5, 4, a()
Pixel 100, 102, RGB(255, 255, 255)
SPRITE SHOW #1, 100, 100, 1
Sides 1

' a background pixel outside the shape: an 8 x 8 sprite opaque only in
' its 2 x 2 middle, over a white pixel at its (0, 0)
For i = 0 To 63 : a(i) = 0 : Next
a(3 * 8 + 3) = RGB(255, 0, 0) : a(3 * 8 + 4) = RGB(255, 0, 0)
a(4 * 8 + 3) = RGB(255, 0, 0) : a(4 * 8 + 4) = RGB(255, 0, 0)
SPRITE LOADARRAY #2, 8, 8, a()
Pixel 150, 100, RGB(255, 255, 255)
SPRITE SHOW #2, 150, 100, 1
Sides 2

' mirrored: a 6 x 4 sprite opaque in its left half, shown mirrored over a
' white pixel at screen offset (4, 1), which its shape now covers
For i = 0 To 23 : a(i) = 0 : If i Mod 6 < 3 Then a(i) = RGB(255, 0, 0)
Next
SPRITE LOADARRAY #3, 6, 4, a()
Pixel 204, 101, RGB(255, 255, 255)
SPRITE SHOW #3, 200, 100, 1, 1
Sides 3
SPRITE SHOW #3, 200, 100, 1, 0
Print "unmirrored:"; SPRITE(B, #3)

' not showing: 0, and its sides are zeroed
SPRITE HIDE #1
Sides 1

Sub Sides(n)
  Local integer k
  Print n; ":"; SPRITE(B, #n); " sides";
  For k = 0 To 7 : Print SPRITE(B, #n, k); : Next
  Print
End Sub
