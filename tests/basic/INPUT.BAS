' INPUT and LINE INPUT
INPUT a%
INPUT "Number"; x
INPUT "Two values", y, s$
INPUT ; z#
LINE INPUT l$
LINE INPUT "Line: "; l$
OPEN "I", #1, "INPUT.TXT"
INPUT #1, a%, x, s$
LINE INPUT #1, l$
CLOSE #1
k$ = INPUT$(1)
OPEN "I", #2, "INPUT.TXT"
k$ = INPUT$(5, #2)
CLOSE
PRINT a%; x; y; s$; z#; l$; k$
END
