' QBOPTS: /Q
' Optimised for speed (undocumented /Q switch: two NOPs after every runtime call)
x% = 5
FOR i = 1 TO 3
  PRINT "line"; i * x%
NEXT i
IF x% > 2 THEN PRINT "big" ELSE PRINT "small"
