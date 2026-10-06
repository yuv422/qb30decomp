' QBOPTS: /D
' Debug build: bounds checks, line tracing, GOSUB via the runtime
DIM a%(10), b(3, 4)
REM $DYNAMIC
DIM c#(5, 5)
TRON
FOR i% = 0 TO 10
    a%(i%) = i% * 3
NEXT i%
FOR i% = 0 TO 3
    FOR j% = 0 TO 4
        b(i%, j%) = i% * j%
        c#(i%, j%) = b(i%, j%) / 2
    NEXT j%
NEXT i%
TROFF
GOSUB Show
x% = 32767
y% = x% - 1
SWAP a%(1), a%(2)
OUT 3, y% AND 255
PRINT a%(1); b(2, 3); c#(1, 1)
END
Show:
    PRINT "show"; a%(5)
    RETURN
