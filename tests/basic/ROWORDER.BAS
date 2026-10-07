' QBOPTS: /R
' Arrays stored in row order (/R)
DIM s(3, 5), t%(2, 3, 4)
FOR i = 0 TO 3
    FOR j = 0 TO 5
        s(i, j) = i * 10 + j
    NEXT j
NEXT i
t%(1, 2, 3) = 7: t%(2, 0, 1) = 9
PRINT s(2, 4); s(3, 1); t%(1, 2, 3); t%(2, 0, 1)
REM $DYNAMIC
n = 4
DIM d(n, 2)
FOR i = 0 TO n
    d(i, 1) = i
NEXT i
PRINT d(3, 1); d(n, 1)
