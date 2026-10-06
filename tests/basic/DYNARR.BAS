' $DYNAMIC arrays
REM $DYNAMIC
n% = 10
DIM a%(n%), b(5, 5), c#(n% * 2), s$(n%)
FOR i% = 0 TO n%
    a%(i%) = i%
    b(i% \ 2, 1) = i% * 1.5
    c#(i%) = i%
    s$(i%) = STR$(i%)
NEXT
x = b(2, 1)
d# = c#(3)
SWAP a%(1), a%(2)
SWAP b(1, 1), x
INPUT b(0, 0)
REDIM a%(20)
a%(15) = 3
v = VARPTR(b(1, 1))
ERASE b, c#
PRINT a%(15); x; d#; s$(3); v
END
