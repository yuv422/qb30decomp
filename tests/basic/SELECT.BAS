' SELECT CASE
n% = 3: x = 2.5: s$ = "b"
SELECT CASE n%
    CASE 1
        PRINT "one"
    CASE 2, 3
        PRINT "two or three"
    CASE 4 TO 6
        PRINT "four to six"
    CASE IS > 10
        PRINT "big"
    CASE ELSE
        PRINT "other"
END SELECT
SELECT CASE x
    CASE IS < 0
        PRINT "negative"
    CASE 0 TO 1.5
        PRINT "small"
    CASE ELSE
        PRINT "large"
END SELECT
SELECT CASE s$
    CASE "a"
        PRINT "a"
    CASE "b", "c"
        PRINT "b or c"
END SELECT
SELECT CASE n% + 1
    CASE 4
        PRINT "four"
END SELECT
END
