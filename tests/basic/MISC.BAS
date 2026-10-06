' Memory, ports, system and miscellaneous statements
DEF SEG = &HB800
POKE 0, 65
p% = PEEK(0)
DEF SEG
a = 40000
DEF SEG = a
OUT &H3C8, 0
i% = INP(&H3C9)
WAIT &H3DA, 8
WAIT &H3DA, 8, 8
x = 1: s$ = "abc"
v% = VARPTR(x)
v% = VARPTR(s$)
v$ = VARPTR$(x)
v% = SADD(s$)
v% = VARPTR(#1)
BLOAD "PIC.BIN", 0
BLOAD "PIC.BIN"
BSAVE "PIC.BIN", 0, 100
f = FRE(0)
f = FRE("")
f = FRE(-1)
SWAP x, y
SWAP s$, t$
SWAP i%, p%
d1# = 1: d2# = 2
SWAP d1#, d2#
TRON
TROFF
SHELL "DIR"
SHELL
ENVIRON "QB=1"
CLEAR
CLEAR , , 2048
OPEN "LPT1:" FOR OUTPUT AS #1
IOCTL #1, "x"
c$ = IOCTL$(1)
CLOSE
DATE$ = "01-01-1990"
TIME$ = "12:00:00"
s% = STICK(0)
b% = STRIG(1)
pn% = PEN(0)
lp% = LPOS(1)
CALL ABSOLUTE(v%)
STOP
SYSTEM
RUN
END
