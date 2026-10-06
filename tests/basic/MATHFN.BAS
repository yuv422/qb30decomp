' Math functions
x = 0.5: d# = 0.25#
y = SIN(x): y = COS(x): y = TAN(x): y = ATN(x)
y = EXP(x): y = LOG(x): y = SQR(x)
e# = SIN(d#): e# = COS(d#): e# = TAN(d#): e# = ATN(d#)
e# = EXP(d#): e# = LOG(d#): e# = SQR(d#)
y = SQR(x * x + 1)
RANDOMIZE 42
RANDOMIZE TIMER
y = RND
y = RND(1)
y = RND(-x)
y = INT(RND * 6) + 1
t = TIMER
y = x ^ 3
e# = d# ^ 0.5
PRINT y, e#, t
END
