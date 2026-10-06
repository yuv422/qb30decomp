' More DEF FN forms
DEF FNmax (a, b)
    IF a > b THEN
        FNmax = a
    ELSE
        FNmax = b
    END IF
END DEF
DEF FNpad$ (s$, n%)
    WHILE LEN(s$) < n%
        s$ = " " + s$
    WEND
    FNpad$ = s$
END DEF
DEF FNsq# (x#) = x# * x#
DEF FNhyp (a, b) = SQR(FNsq#(a) + FNsq#(b))
DEF FNnoargs% = 42
k = 7
PRINT FNmax(3, k), FNpad$("x", 5), FNhyp(3, 4), FNnoargs%
IF FNmax(k, 10) > 8 THEN PRINT "big"
z = FNmax(FNmax(1, 2), FNmax(k, 3)) + 1
PRINT z
END
