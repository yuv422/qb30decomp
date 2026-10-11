' Unreachable code. The compiler drops statements after GOTO, RETURN or
' END up to the next label or line number, so the decompiler must give
' unreachable code a label for it to survive recompilation.
10 PRINT "start"
GOTO 30
20 PRINT "skipped"
x = 1
30 PRINT "middle"
GOSUB 100
IF x = 0 THEN GOTO 40
PRINT "x is not 0"
40 END
50 PRINT "after end"
100 PRINT "sub"
RETURN
110 PRINT "unused sub"
y = 2
RETURN
