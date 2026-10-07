' COMMON with arrays and strings, arrays above 7FFFh, VARPTR of arrays
DIM shared1(100), names$(5), counts%(10)
COMMON level%, shared1(), player$, names$(), counts%(), score#
DIM big%(16000)
DIM menu$(2), opts$(1)
level% = 3: player$ = "Ann": score# = 12.5
names$(2) = "Bob": counts%(4) = 7: shared1(50) = 1.5
menu$(0) = "New": menu$(1) = "Load": menu$(2) = "Quit"
opts$(0) = "On": opts$(1) = "Off"
FOR i = 0 TO 2
    PRINT menu$(i); " "; opts$(i MOD 2)
NEXT i
DEF SEG
POKE VARPTR(big%(0)), 65
PRINT PEEK(VARPTR(big%(0))); big%(0)
BSAVE "BIG.DAT", VARPTR(big%(0)), 200
BLOAD "BIG.DAT", VARPTR(counts%(0))
KILL "BIG.DAT"
PRINT counts%(0); names$(2); level%; player$; score#
CHAIN "NEXTPROG"
