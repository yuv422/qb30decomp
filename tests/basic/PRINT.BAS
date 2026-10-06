' PRINT, WRITE and LPRINT forms
a% = 1: x = 2.5: d# = 3.5#: s$ = "str"
PRINT
PRINT a%
PRINT a%;
PRINT a%,
PRINT x; d#; s$
PRINT "a", "b"; "c"
PRINT TAB(10); "tab"; SPC(3); "spc"
PRINT USING "##.##"; x
PRINT USING "& ###"; s$; a%;
PRINT USING "$$#,###.##"; d#, x
WRITE a%, x, s$
WRITE
OPEN "O", #1, "PRINT.TXT"
PRINT #1, a%; x; s$
PRINT #1, USING "##.#"; x
WRITE #1, s$, a%
CLOSE #1
LPRINT "printer"; a%
LPRINT USING "###"; a%
WIDTH 80
WIDTH LPRINT 132
PRINT a% + 1; x * 2; s$ + "x"
END
