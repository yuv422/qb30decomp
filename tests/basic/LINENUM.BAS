' QBOPTS: /X
' Line numbers inside blocks, loops and SUBs (kept for ERL with /X)
10 ON ERROR GOTO 900
x = 1
IF x = 1 THEN
  20 PRINT "a"
30 ELSE
  PRINT "b"
40 END IF
DO
  50 x = x + 1
60 LOOP WHILE x < 5
FOR i = 1 TO 2
  70 PRINT i
80 NEXT i
85 IF x > 2 THEN GOSUB 1000
90 CALL s(x)
95 ERROR 11
END
900 PRINT "error"; ERR; "in line"; ERL
910 RESUME NEXT
1000 PRINT "sub at"; x
1010 RETURN
SUB s (a) STATIC
  100 a = 1
110 END SUB
