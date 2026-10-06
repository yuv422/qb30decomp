' Static arrays
DIM a%(10), b(5, 3), c#(20), s$(8)
DIM m%(2, 3, 4)
FOR i% = 0 TO 10
    a%(i%) = i% * i%
NEXT
FOR i% = 0 TO 5
    FOR j% = 0 TO 3
        b(i%, j%) = i% + j% / 10
    NEXT j%
NEXT i%
c#(3) = 1.5#
c#(a%(2)) = c#(3) * 2
s$(1) = "one"
s$(2) = s$(1) + "two"
m%(1, 2, 3) = 7
k% = m%(1, 2, 3) + a%(10)
SWAP a%(1), a%(2)
SWAP s$(1), s$(2)
SWAP b(0, 0), b(1, 1)
PRINT a%(5); b(2, 1); c#(4); s$(2); LBOUND(a%); UBOUND(b, 2)
ERASE a%, s$
END
