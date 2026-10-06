' GOTO, GOSUB, ON GOTO and line numbers
10 n% = 1
20 GOSUB 100
30 ON n% GOTO 40, 50, 60
40 PRINT "forty": GOTO 70
50 PRINT "fifty": GOTO 70
60 PRINT "sixty"
70 ON n% GOSUB 200, 300
80 n% = n% + 1
90 IF n% < 4 THEN 20
95 GOTO Finish
100 PRINT "sub 100"
110 RETURN
200 PRINT "sub 200": RETURN
300 PRINT "sub 300"
310 RETURN 80
Finish:
    GOSUB LocalSub
    END
LocalSub:
    PRINT "local"
    RETURN
