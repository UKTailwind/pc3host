' TilemapLoadTest.bas - exercises TILEMAP LOAD against TILEMAP CREATE.
'
' Self-contained: draws its own tileset into flash slot 1, writes its own
' map files, then checks that a map loaded from a file is identical to
' the same map read from DATA, that the default extension and re-loading
' a used slot work, that every malformed file is refused with the right
' message, and that a 256 x 256 map loads.  Finishes by drawing the map
' and saving tmtest.bmp.  Needs MODE 2 and a drive for the files.
Option EXPLICIT
Option BASE 0

Const TW = 16, TH = 8, TPR = 8
Const COLS = 20, ROWS = 30

Dim integer fails, tests, c, r, a, b, n, v
Dim float t0
Dim s$

MODE 2
CLS
FRAMEBUFFER CREATE
MakeTileset
Flash LOAD IMAGE 1, "tmtest_tiles.bmp", O

' ---- the reference map as a file, with comments and mixed separators
Open "tmtest.map" For Output As #1
Print #1, "' tilemap test file"
Print #1, "  20 , 30   # width and height"
Print #1, ""
Restore mapdata
For r = 0 To ROWS - 1
  s$ = ""
  For c = 0 To COLS - 1
    Read n
    If c = 0 Then
      s$ = Str$(n)
    ElseIf r Mod 2 = 0 Then
      s$ = s$ + "," + Str$(n)
    Else
      s$ = s$ + Chr$(9) + Str$(n)
    EndIf
  Next c
  Print #1, s$ + "  ' row" + Str$(r)
Next r
Print #1, "# trailing comment with no newline";
Close #1

' ---- CREATE from DATA against LOAD from the file
Tilemap CLOSE
Tilemap CREATE mapdata, 1, 1, TW, TH, TPR, COLS, ROWS
Tilemap LOAD "tmtest.map", 2, 1, TW, TH, TPR
Check "loaded cols", Tilemap(COLS 2), COLS
Check "loaded rows", Tilemap(ROWS 2), ROWS
n = 0
For r = 0 To ROWS - 1
  For c = 0 To COLS - 1
    a = Tilemap(TILE 1, c * TW + TW \ 2, r * TH + TH \ 2)
    b = Tilemap(TILE 2, c * TW + TW \ 2, r * TH + TH \ 2)
    If a <> b Then n = n + 1
  Next c
Next r
Check "cells differing from CREATE", n, 0

' ---- default extension, and loading into a slot that is already in use
Tilemap LOAD "tmtest", 3, 1, TW, TH, TPR
Check "default .map extension, cols", Tilemap(COLS 3), COLS
Tilemap LOAD "tmtest.map", 2, 1, TW, TH, TPR
Check "reload into used slot, rows", Tilemap(ROWS 2), ROWS
Check "reload keeps a cell", Tilemap(TILE 2, 5 * TW + 1, 7 * TH + 1), Tilemap(TILE 1, 5 * TW + 1, 7 * TH + 1)

' ---- malformed files
WriteFile "tmbad1.map", "20"
ExpectError "size missing", "tmbad1.map", "width and height"
WriteFile "tmbad2.map", "0, 30, 1, 2, 3"
ExpectError "zero width", "tmbad2.map", "width and height"
WriteFile "tmbad3.map", "20, 30, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10"
ExpectError "too few values", "tmbad3.map", "need 600, found 10"
WriteFile "tmbad4.map", "20 30 5 5 x 5"
ExpectError "invalid character", "tmbad4.map", "Invalid character"
WriteFile "tmbad5.map", "20 30 5 70000 5"
ExpectError "value out of range", "tmbad5.map", "out of range"
WriteFile "tmbad6.map", ""
ExpectError "empty file", "tmbad6.map", "width and height"
ExpectError "missing file", "tmnothere.map", ""
' after all that, slot 2 must still be intact
Check "slot 2 survives failed loads", Tilemap(COLS 2), COLS

' ---- the real breakout map file (Bas/breakout.map): walls of 5, bricks 1-4
Tilemap LOAD "breakout.map", 3, 1, TW, TH, TPR
Check "breakout.map cols", Tilemap(COLS 3), 20
Check "breakout.map rows", Tilemap(ROWS 3), 30
Check "breakout.map top wall", Tilemap(TILE 3, 9 * TW + 1, 1), 5
Check "breakout.map red brick", Tilemap(TILE 3, 1 * TW + 1, 4 * TH + 1), 1
Check "breakout.map blue brick", Tilemap(TILE 3, 18 * TW + 1, 11 * TH + 1), 4
Check "breakout.map empty", Tilemap(TILE 3, 10 * TW + 1, 20 * TH + 1), 0

' ---- a 256 x 256 map: 65536 values, 128 KB of map
Print "writing tmbig.map ..."
Open "tmbig.map" For Output As #1
Print #1, "256 256"
For r = 0 To 255
  For c = 0 To 255 Step 16
    s$ = ""
    For n = c To c + 15
      s$ = s$ + Str$((n * 257 + r * 3) Mod 65536) + " "
    Next n
    Print #1, s$
  Next c
Next r
Close #1
t0 = Timer
Tilemap LOAD "tmbig.map", 4, 1, TW, TH, TPR
Print "256x256 load took" + Str$(Int(Timer - t0)) + " ms"
Check "big cols", Tilemap(COLS 4), 256
Check "big rows", Tilemap(ROWS 4), 256
n = 0
For r = 0 To 255 Step 5
  For c = 0 To 255 Step 3
    v = (c * 257 + r * 3) Mod 65536
    If Tilemap(TILE 4, c * TW + 1, r * TH + 1) <> v Then n = n + 1
  Next c
Next r
Check "big cells sampled, mismatches", n, 0
Check "big last cell", Tilemap(TILE 4, 255 * TW + 1, 255 * TH + 1), (255 * 257 + 255 * 3) Mod 65536

' ---- draw the loaded map so it can be looked at
FRAMEBUFFER WRITE F
CLS
Tilemap DRAW 2, F, 0, 0, 0, 0, 320, 240
Text 160, 236, "TILEMAP LOAD test", "CB", 1, 1, RGB(WHITE)
FRAMEBUFFER COPY F, N
FRAMEBUFFER WRITE N
Save IMAGE "tmtest.bmp"

Print "TILEMAP LOAD:" + Str$(tests - fails) + " of" + Str$(tests) + " checks passed"
If fails = 0 Then Print "PASS" Else Print "FAIL"
Tilemap CLOSE
End

Sub Check(what$, got As integer, want As integer)
  tests = tests + 1
  If got = want Then
    Print "ok:   " + what$ + " =" + Str$(got)
  Else
    Print "FAIL: " + what$ + " got" + Str$(got) + " want" + Str$(want)
    fails = fails + 1
  EndIf
End Sub

' Load file$ into slot 4 expecting an error whose message contains must$
' (any error if must$ is empty)
Sub ExpectError(what$, file$, must$)
  Local m$
  tests = tests + 1
  On Error Skip 1
  Tilemap LOAD file$, 4, 1, TW, TH, TPR
  m$ = MM.ErrMsg$
  If MM.ErrNo = 0 Then
    Print "FAIL: " + what$ + " - no error raised"
    fails = fails + 1
  ElseIf must$ <> "" And Instr(m$, must$) = 0 Then
    Print "FAIL: " + what$ + " - got '" + m$ + "'"
    fails = fails + 1
  Else
    Print "ok:   " + what$ + " ->" + m$
  EndIf
  On Error Clear
End Sub

Sub WriteFile(name$, content$)
  Open name$ For Output As #2
  Print #2, content$;
  Close #2
End Sub

' eight 16 x 8 tiles in a row: black, then the seven primary colours
Sub MakeTileset
  Local i
  CLS RGB(BLACK)
  For i = 1 To 7
    Box i * TW, 0, TW, TH, 0, RGB(255 * (i And 1), 255 * ((i And 2) \ 2), 255 * ((i And 4) \ 4)), RGB(255 * (i And 1), 255 * ((i And 2) \ 2), 255 * ((i And 4) \ 4))
    Box i * TW + 1, 1, TW - 2, TH - 2, 1, RGB(WHITE)
  Next i
  Save IMAGE "tmtest_tiles.bmp", 0, 0, TPR * TW, TH
End Sub

' 20 x 30: walls of 5 round the edge, a pattern of 0..7 inside
mapdata:
Data 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
Data 5,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,5
Data 5,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,5
Data 5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,5
Data 5,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5
Data 5,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,5
Data 5,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,5
Data 5,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,5
Data 5,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,5
Data 5,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,5
Data 5,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,5
Data 5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,5
Data 5,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5
Data 5,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,5
Data 5,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,5
Data 5,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,5
Data 5,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,5
Data 5,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,5
Data 5,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,5
Data 5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,5
Data 5,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5
Data 5,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,5
Data 5,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,5
Data 5,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,5
Data 5,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,5
Data 5,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,5
Data 5,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,5
Data 5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5,0,3,5
Data 5,7,2,5,0,3,6,1,4,7,2,5,0,3,6,1,4,7,2,5
Data 5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
