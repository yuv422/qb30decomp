' QBOPTS: /E /X
' Error trapping
ON ERROR GOTO Handler
ERROR 5
z = 0: x = 1 / z
PRINT "after"
ON ERROR GOTO 0
END
Handler:
    PRINT "error"; ERR; ERL
    IF ERR = 5 THEN RESUME NEXT
    IF ERR = 11 THEN RESUME Done
    RESUME
Done:
    d% = ERDEV
    d$ = ERDEV$
    END
