' The PicoMite's sprite fixes of 20-21 October, by their pixels: #89
' (SWAP), #90 (WRITE, and the opaque orientations kept with the sprite)
' and #93 (a BMP that fails to decode).  The bookkeeping is the FUZIX
' gate test tests/sprite8.bas.  good16.bmp is 16 x 16 and red;
' bad16.bmp is the same file cut off after three rows.
MODE 2
Dim integer red(255), spot(255), dark(63), i
For i = 0 To 255 : red(i) = RGB(255, 0, 0) : spot(i) = 0 : Next
spot(8 * 16 + 8) = RGB(255, 255, 255)
For i = 0 To 63 : dark(i) = 0 : Next
CLS RGB(0, 0, 255)

' #89: SWAP draws the new image over the background stored under the
' old one, so none of the old stays where the new is transparent
SPRITE LOADARRAY #1, 16, 16, red()
SPRITE LOADARRAY #2, 16, 16, spot()
SPRITE SHOW #1, 50, 50, 1
SPRITE SWAP #1, #2
Print "swap: "; Hex$(Pixel(52, 52)); " "; Hex$(Pixel(58, 58))

' #90: WRITE stamps a copy; the sprite stays where it is shown
SPRITE LOADARRAY #3, 16, 16, red()
SPRITE SHOW #3, 50, 100, 1
SPRITE WRITE #3, 150, 100
SPRITE HIDE #3
Print "write: "; Hex$(Pixel(55, 105)); " "; Hex$(Pixel(155, 105))

' #90: orientation 4 draws the transparent colour too, and the sprite
' keeps it: MOVE and SHOW SAFE draw its black
SPRITE LOADARRAY #4, 8, 8, dark()
SPRITE SHOW #4, 100, 150, 1, 4
SPRITE NEXT #4, 120, 150
SPRITE MOVE
Print "move: "; Hex$(Pixel(104, 154)); " "; Hex$(Pixel(124, 154))
SPRITE SHOW SAFE #4, 140, 150, 1, 4
Print "safe: "; Hex$(Pixel(124, 154)); " "; Hex$(Pixel(144, 154))

' #93: a BMP that fails to decode leaves no buffer in use, so the next
' load into it works
On Error Skip
SPRITE LOADBMP #5, "bad16"
Print "bad: "; MM.ErrNo <> 0
SPRITE LOADBMP #5, "good16"
SPRITE SHOW #5, 200, 200, 1
Print "good:"; SPRITE(W, #5); " "; Hex$(Pixel(208, 208))
