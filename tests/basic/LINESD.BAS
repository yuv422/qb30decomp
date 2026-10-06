' QBOPTS: /D
' Numbered lines in a /D build (TRON prints them)
TRON
10 x = 1
20 GOTO 40
30 PRINT "a"
40 PRINT x: y = 2
PRINT y
50 IF y = 2 THEN 70
60 PRINT "no"
70 TROFF
80 END
