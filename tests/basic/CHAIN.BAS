' COMMON, CHAIN and RUN
DIM c(5)
COMMON a%, b$, c()
a% = 1
b$ = "hello"
c(2) = 3.5
IF a% = 2 THEN RUN "OTHER"
IF a% = 3 THEN RUN
CHAIN "NEXTPROG"
END
