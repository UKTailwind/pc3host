' pc3bench - where the time goes on THIS machine.
'
' Every graphics statement on a PC is a round trip to the display
' server; on the board it is a microsecond ioctl.  When a program runs
' slowly on one PC and not another, these numbers say which part is
' slow: the round trip itself, the interpreter, the frame copy, the
' keyboard poll, or the text.  Run it and compare with the reference
' column, which is a fast machine under WSL.
'
'   pc3cc -r /opt/pc3/share/examples/pc3bench.bas
'
MODE 2
Dim integer i, c, n
Dim float t
Dim string k

Print "pc3bench"
Print "                                    this machine     reference"

Timer = 0
For i = 1 To 5000
  c = Pixel(1, 1)
Next i
t = Timer
Print "round trip (GETPIXEL)          "; Str$(t * 1000 / 5000, 6, 1); " us      27 us"

Timer = 0
For i = 1 To 2000000
  n = n + i
Next i
t = Timer
Print "interpreter, 2M adds           "; Str$(t, 6, 0); " ms     "; "  (about 130 ms)"

FRAMEBUFFER CREATE
Timer = 0
For i = 1 To 200
  FRAMEBUFFER COPY F, N
Next i
t = Timer
Print "FRAMEBUFFER COPY F, N          "; Str$(t * 1000 / 200, 6, 1); " us      51 us"

Timer = 0
For i = 1 To 200
  FRAMEBUFFER COPY F, N, B
Next i
t = Timer
Print "FRAMEBUFFER COPY F, N, B       "; Str$(t / 200, 6, 2); " ms   16.80 ms (one frame)"

Timer = 0
For i = 1 To 1000
  k = Inkey$
Next i
t = Timer
Print "INKEY$ with nothing pressed    "; Str$(t * 1000 / 1000, 6, 1); " us      10 us (from a terminal)"

Timer = 0
For i = 1 To 1000
  Box 10, 10, 100, 60, 1, 7, 0
Next i
t = Timer
Print "BOX 90x50 filled               "; Str$(t * 1000 / 1000, 6, 1); " us     246 us"

Timer = 0
For i = 1 To 300
  Text 4, 1, "SCORE:" + Str$(i), "LT", 1, 1, 15
Next i
t = Timer
Print "TEXT (one crossing each)       "; Str$(t * 1000 / 300, 6, 1); " us      72 us"

Timer = 0
For i = 1 To 100
  CLS 0
Next i
t = Timer
Print "CLS                            "; Str$(t * 1000 / 100, 6, 1); " us      74 us"

FRAMEBUFFER CLOSE
Print "done - paste this with the last line pc3d --verbose prints on exit"
