' Multi dimensional dynamic arrays and array parameters
REM $DYNAMIC
n% = 4
DIM g(n%, n%, 2), w$(n%, 3)
FOR i% = 0 TO n%
    FOR j% = 0 TO n%
        g(i%, j%, 1) = i% * j%
    NEXT
    w$(i%, 2) = CHR$(65 + i%)
NEXT
CALL Total(g(), t)
PRINT t; w$(3, 2)
END

SUB Total (arr(3), r) STATIC
    r = 0
    FOR i% = 0 TO 4
        r = r + arr(i%, i%, 1)
    NEXT
END SUB
