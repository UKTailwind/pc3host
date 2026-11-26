' PLAY MODSAMPLE through playmod (V8.00.00b2): the start with a rate, a
' pan and LOOP, CHANGE and STOP, empty arguments and fractions among
' them.  What playmod asked hxcmod to do goes to the file PLAYMOD_TRACE
' names, for modsample_calls.expected: each is the call the PicoMite's
' ModSampleCommand makes for the same statement.  doppler.mod is
' Bas/doppler/doppler.mod from the PicoMite tree.
Open "modsample.out" For Output As #1
PLAY MODSAMPLE STOP
PLAY MODSAMPLE STOP 2
Print #1, "stop without a MOD file"
PLAY MODFILE "doppler.mod"
PAUSE 200
PLAY MODSAMPLE 10, 1
PLAY MODSAMPLE 11, 2, 0
PLAY MODSAMPLE 1, 3, 64, 22050
PLAY MODSAMPLE 31, 4, 1, 150, -100
PLAY MODSAMPLE 10, 1, 40, 16000, -100, LOOP
PLAY MODSAMPLE 11, 2, LOOP
PLAY MODSAMPLE 11, 3, , , LOOP
PLAY MODSAMPLE 10, 4, , 500000, 37.5
PLAY MODSAMPLE CHANGE 1
PLAY MODSAMPLE CHANGE 1, , , 0
PLAY MODSAMPLE CHANGE 1, 32.4, 11025.5, -99.5
PLAY MODSAMPLE CHANGE 2, 0
PLAY MODSAMPLE CHANGE 3, , 8000
PLAY MODSAMPLE STOP 1
PLAY MODSAMPLE STOP
Print #1, "samples sent"
PAUSE 300
PLAY STOP
Close #1
