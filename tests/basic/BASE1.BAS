' OPTION BASE 1 and multi dimensional arrays
OPTION BASE 1
DIM a%(10), m(3, 4, 5), s$(2, 3)
FOR i% = 1 TO 10
    a%(i%) = i%
NEXT
FOR i% = 1 TO 3
    FOR j% = 1 TO 4
        FOR k% = 1 TO 5
            m(i%, j%, k%) = i% + j% * 10 + k% * 100
        NEXT k%
    NEXT j%
NEXT i%
s$(1, 1) = "a"
s$(2, 3) = s$(1, 1) + "b"
n% = 2
PRINT a%(n%); m(n%, n% + 1, 5); s$(n%, 3)
END
