' IF statements in all their forms
a% = 3: x = 2.5: s$ = "yes"
IF a% = 3 THEN PRINT "three"
IF a% <> 3 THEN PRINT "not three" ELSE PRINT "is three"
IF x > 1 THEN
    PRINT "big"
END IF
IF x < 1 THEN
    PRINT "small"
ELSE
    PRINT "not small"
END IF
IF a% = 1 THEN
    PRINT "one"
ELSEIF a% = 2 THEN
    PRINT "two"
ELSEIF a% = 3 THEN
    PRINT "three"
ELSE
    PRINT "many"
END IF
IF s$ = "yes" AND a% > 2 THEN
    IF x <> 0 THEN
        PRINT "nested"
    END IF
    PRINT "after"
END IF
IF a% THEN PRINT "nonzero"
IF NOT a% THEN PRINT "zero"
IF x THEN PRINT "x nonzero"
IF a% > 2 OR x < 1 THEN PRINT "or"
IF s$ <> "" THEN PRINT s$
IF a% = 3 THEN 100
PRINT "skipped"
100 PRINT "line 100"
END
