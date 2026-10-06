' Type conversion functions
x = 2.75: d# = -3.5#: i% = 9
PRINT CINT(x), CINT(d#), CINT(x + 1)
PRINT CSNG(d#), CSNG(i%), CSNG(d# * 2)
PRINT CDBL(x), CDBL(i%), CDBL(x * x)
PRINT FIX(x), FIX(d#), FIX(i%)
PRINT INT(x), INT(d#), INT(i%), INT(x / 3)
PRINT ABS(x), ABS(d#), ABS(i%), ABS(x - 5)
PRINT SGN(x), SGN(d#), SGN(i%), SGN(x - 5)
i% = x
i% = d#
x = i%
d# = i%
x = d#
d# = x
END
