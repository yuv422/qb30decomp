' QBOPTS: /V
' Event trapping checked between statements
ON TIMER(1) GOSUB Tick
TIMER ON
FOR i% = 1 TO 1000: n% = n% + 1: NEXT
TIMER OFF
ON KEY(2) GOSUB Pressed
KEY(2) ON
DO
    k$ = INKEY$
LOOP UNTIL k$ <> ""
END
Tick:
    t% = t% + 1
    RETURN
Pressed:
    PRINT "F2"
    RETURN
