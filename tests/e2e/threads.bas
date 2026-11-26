' threads.bas - THREAD on a PC: the other threads run while the main
' program waits in INPUT, and a thread's SETTICK goes on meanwhile
' (mmx_io_wait).  run.sh gives it its line only after 0.3 s.
Dim Integer n, ticks
Dim a$
Sub Busy
  Do : Inc n : Loop
End Sub
Sub Tick
  Inc ticks
End Sub
Sub Ticker
  SetTick 10, Tick, 1
  Do : Pause 1000 : Loop
End Sub
Thread Start 1, Busy
Thread Start 2, Ticker
Input "name"; a$
Print
Print "got "; a$
Print "the busy thread ran meanwhile:"; n > 1000
Print "thread 2's tick ran meanwhile:"; ticks >= 10
Thread Stop 1 : Thread Stop 2
