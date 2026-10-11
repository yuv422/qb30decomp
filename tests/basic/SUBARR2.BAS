' Array parameters: UBOUND/LBOUND of a parameter, a SUB passing its own
' array parameter on, an array passed whole (its DIM comes from the call)
' and a SUB that is never called (parameters from its body)
DIM m$(3, 5), v%(7)
m$(0, 2) = "x"
v%(2) = 4
CALL outer(m$(), v%(), 3)
END
SUB inner(a$(2), n%) STATIC
  PRINT UBOUND(a$, 1); UBOUND(a$, 2); LBOUND(a$, 2); n%
END SUB
SUB outer(b$(2), w%(1), k%) STATIC
  FOR i% = 0 TO UBOUND(b$, 2)
    PRINT b$(0, i%);
  NEXT i%
  PRINT UBOUND(w%)
  CALL inner(b$(), k%)
END SUB
SUB unused(z$(1), q, d#(1), j%) STATIC
  PRINT z$(1); q; VARPTR(d#(j%))
END SUB
