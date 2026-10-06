' FOR, WHILE and DO loops
FOR i% = 1 TO 10
    PRINT i%
NEXT i%
FOR x = 10 TO 1 STEP -1.5
    PRINT x
NEXT x
FOR d# = 0 TO 1 STEP 0.25#
    PRINT d#
NEXT d#
n% = 5
FOR i% = n% TO n% * 2 STEP 2
    FOR j% = 1 TO i%
        PRINT j%;
    NEXT j%
    PRINT
NEXT i%
s% = -1
FOR i% = 10 TO 0 STEP s%
    IF i% = 3 THEN EXIT FOR
NEXT
WHILE n% > 0
    n% = n% - 1
WEND
DO
    n% = n% + 1
LOOP UNTIL n% >= 10
DO
    n% = n% - 2
LOOP WHILE n% > 0
DO WHILE n% < 5
    n% = n% + 1
LOOP
DO UNTIL n% = 0
    n% = n% - 1
    IF n% = 2 THEN EXIT DO
LOOP
DO
    n% = n% + 1
    IF n% > 100 THEN EXIT DO
LOOP
END
