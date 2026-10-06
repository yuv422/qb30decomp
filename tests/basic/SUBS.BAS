' SUB procedures and DEF FN functions
DEF FNsq (x) = x * x
DEF FNadd% (a%, b%) = a% + b%
DEF FNcat$ (a$, b$) = a$ + "-" + b$
DEF FNfact# (n%)
    r# = 1
    FOR i% = 2 TO n%
        r# = r# * i%
    NEXT
    IF n% < 0 THEN EXIT DEF
    FNfact# = r#
END DEF
DIM arr%(5)
total = 0
CALL Hello
CALL AddUp(3, 4, total)
CALL Fill(arr%(), 5)
v = 2
CALL AddUp(v, v * 2, total)
PRINT FNsq(3), FNadd%(1, 2), FNcat$("a", "b"), FNfact#(5), total, arr%(3)
END

SUB Hello STATIC
    PRINT "hello"
END SUB

SUB AddUp (a, b, r) STATIC
    SHARED total
    r = a + b
    IF r > 100 THEN EXIT SUB
    total = total + r
END SUB

SUB Fill (x%(1), n%) STATIC
    STATIC count%
    FOR i% = 0 TO n%
        x%(i%) = i% * 2
    NEXT
    count% = count% + 1
END SUB
