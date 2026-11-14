' ON KEY k counts only a k that comes while it is armed (PicoMite
' 641d043, the 11-13 review's row 14).  run.sh presses A at +0.5 s,
' before ON KEY 97 is armed at +1.5 s, and again at +2.5 s, counted from
' when the "start" file appears.  Each event is written with the second
' it happened in.  The first A is an ordinary key, which INKEY$ reads as
' soon as the loop starts (second 1); the second calls the handler
' (second 2) and never reaches INKEY$.  Before the fix the first called
' the handler the moment it was armed, and INKEY$ could take the second
' before ON KEY saw it.
k$ = INKEY$                    ' hold the console from here
OPEN "onkeyarm.out" FOR OUTPUT AS #1
OPEN "start" FOR OUTPUT AS #2
CLOSE #2
PRINT "start"
t = TIMER
DO WHILE TIMER < t + 1500
LOOP
ON KEY 97, Hit
DO WHILE TIMER < t + 3500
  k$ = INKEY$
  IF k$ <> "" THEN PRINT #1, "key"; ASC(k$); " at"; (TIMER - t) \ 1000
LOOP
PRINT #1, "end"
CLOSE #1
PRINT "finished"
END

SUB Hit
  PRINT #1, "call at"; (TIMER - t) \ 1000
END SUB
