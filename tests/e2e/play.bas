' PLAY through the server: the synth (SOUND, TONE with its completion
' interrupt, VOLUME, STOP) and a player (WAV) on the PCM ring - twice,
' to prove STOP hands the output back.  Before phase 3 the first PLAY
' SOUND raised "Sound output did not start".  What happened goes to
' play.out for the gate; snd.wav is a 5 s tone the script puts beside it.
Dim integer done, t
Open "play.out" For Output As #1
PLAY VOLUME 70, 70
PLAY SOUND 1, B, S, 440, 25
PAUSE 100
PLAY SOUND 1, B, O
PLAY TONE 880, 880, 120, tdone
t = Timer
Do : PAUSE 10 : Loop Until done Or Timer - t > 2000
If done Then Print #1, "tone done" Else Print #1, "tone timeout"
PLAY STOP
Print #1, "stopped"
PLAY WAV "snd.wav"
PAUSE 300
PLAY STOP
PAUSE 200
PLAY WAV "snd.wav"
PAUSE 300
PLAY STOP
Print #1, "wav twice"
' MM.INFO(BBC BUFFER [ch]): the first note plays at once, two wait in
' channel 1's queue of 16; PLAY STOP empties them
PLAY BBC SOUND 1, -15, 100, 20
PLAY BBC SOUND 1, -15, 100, 20
PLAY BBC SOUND 1, -15, 100, 20
Print #1, "bbc buffer"; MM.Info(BBC BUFFER 1); MM.Info(BBC BUFFER); MM.Info(BBC BUFFER 0)
PLAY STOP
Print #1, "bbc buffer"; MM.Info(BBC BUFFER)
Close #1
End

Sub tdone
  done = 1
End Sub
