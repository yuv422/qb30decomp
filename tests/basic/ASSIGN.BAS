' Variable types, literals and assignment
a% = 5
b% = -32768
c% = 32767
d = 3.5
e = -0.125
f = 1E+20
g# = 3.14159265358979#
h# = -1D-100
s$ = "hello"
t$ = ""
u$ = s$
a% = c%
d = e
g# = h#
d = a%
a% = d
g# = d
d = g#
a% = g#
g# = a%
x! = 2.5
PRINT a%, b%, c%, d, e, f, g#, h#, s$, t$, u$, x!
END
