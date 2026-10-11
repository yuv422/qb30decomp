' QBOPTS: /X
' With /E or /X every statement (END IF and the GOTO of IF ... THEN GOTO
' included) takes a line table entry: simple IFs are written on one line
10 ON ERROR GOTO 100
A = 1: B = 2
IF A = 1 THEN B = 5: C = 6 ELSE D = 7
IF A = 2 THEN
  B = 8
  C = 9
END IF
IF A = 3 THEN 50
IF A = 4 THEN B = 1 ELSE 60
PRINT B; C; D
50 B = 2
60 PRINT A; B
END
100 RESUME NEXT
