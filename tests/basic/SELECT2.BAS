' SELECT CASE: harder forms
k% = 7: lim% = 9: a$ = "m"
FOR i% = 0 TO 12
    SELECT CASE i%
        CASE 1 TO 2, 5
            PRINT "1-2 or 5"
        CASE 3
        CASE 9, 10
            SELECT CASE k% * 2
                CASE 14
                    PRINT "nested"
                CASE ELSE
                    PRINT "nested else"
            END SELECT
        CASE 4, 6 TO 8, IS >= 11
            IF i% = 11 THEN EXIT FOR
            PRINT "multi"
        CASE ELSE
            PRINT "else"
    END SELECT
NEXT i%
DO
    SELECT CASE a$
        CASE "a" TO "f", "z"
            PRINT "early"
        CASE ELSE
            a$ = "a"
    END SELECT
    n% = n% + 1
LOOP WHILE n% < 3
SELECT CASE k%
    CASE ELSE
        PRINT "only else"
END SELECT
SELECT CASE k% MOD 3
    CASE 0
        GOTO done
    CASE 1
        PRINT "one"
END SELECT
done:
x# = 1.25
SELECT CASE x# * 2
    CASE IS <= 2.5#
        PRINT "le"
END SELECT
END
