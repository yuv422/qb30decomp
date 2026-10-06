' Relational and logical operators
a% = 5: b% = 9: x = 1.5: y = 2.5: d# = 3: s$ = "abc": t$ = "abd"
c% = a% = b%
c% = a% <> b%
c% = a% < b%
c% = a% <= b%
c% = a% > b%
c% = a% >= b%
c% = x < y
c% = d# >= x
c% = s$ = t$
c% = s$ < t$
c% = a% AND b%
c% = a% OR b%
c% = a% XOR b%
c% = NOT a%
c% = a% EQV b%
c% = a% IMP b%
c% = (a% < b%) AND (x > y)
c% = (a% < b%) OR NOT (x > y)
c% = x AND y
PRINT c%
END
