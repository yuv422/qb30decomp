' DATA, READ and RESTORE
DATA 1, 2.5, "three", four
100 DATA -7, 1E10, 3.25#
DATA ""
READ a%, x, s$, t$
READ b%, y, z#
READ e$
RESTORE
READ c%
RESTORE 100
READ d%
PRINT a%; x; s$; t$; b%; y; z#; c%; d%; e$
END
