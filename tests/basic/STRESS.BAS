' Larger program mixing many features
DEFINT I-N
DIM names$(20), scores(20), hist%(10)
DEF FNclamp(v, lo, hi)
    IF v < lo THEN v = lo
    IF v > hi THEN v = hi
    FNclamp = v
END DEF
DEF FNavg(a, b) = (a + b) / 2
RANDOMIZE 42
n = 0
RESTORE people
READ cnt
FOR i = 1 TO cnt
    READ names$(i), scores(i)
    n = n + 1
NEXT i
' bubble sort descending
swapped = -1
WHILE swapped
    swapped = 0
    FOR i = 1 TO n - 1
        IF scores(i) < scores(i + 1) THEN
            SWAP scores(i), scores(i + 1)
            SWAP names$(i), names$(i + 1)
            swapped = -1
        END IF
    NEXT i
WEND
CALL Report(names$(), scores(), n)
' histogram
FOR i = 1 TO n
    b = INT(FNclamp(scores(i), 0, 99) / 10)
    hist%(b) = hist%(b) + 1
NEXT i
FOR b = 0 TO 9
    IF hist%(b) > 0 THEN PRINT USING "##-##: "; b * 10; b * 10 + 9; : PRINT STRING$(hist%(b), "*")
NEXT b
' string work
s$ = ""
FOR i = 1 TO n
    s$ = s$ + LEFT$(names$(i), 1)
NEXT i
PRINT "initials: "; s$; " reversed: ";
FOR i = LEN(s$) TO 1 STEP -1
    PRINT MID$(s$, i, 1);
NEXT i
PRINT
k = INSTR(s$, "C")
IF k THEN PRINT "C at"; k ELSE PRINT "no C"
' file round trip
OPEN "STRESS.DAT" FOR OUTPUT AS #1
FOR i = 1 TO n
    WRITE #1, names$(i), scores(i)
NEXT i
CLOSE #1
OPEN "STRESS.DAT" FOR INPUT AS #1
total = 0
DO WHILE NOT EOF(1)
    INPUT #1, nm$, sc
    total = total + sc
LOOP
CLOSE #1
KILL "STRESS.DAT"
PRINT "total"; total; "avg"; FNavg(total / n, scores(1))
' menu with ON GOTO and GOSUB
FOR choice = 1 TO 3
    ON choice GOTO m1, m2, m3
m1: GOSUB Banner: GOTO mnext
m2: PRINT "two": GOTO mnext
m3: PRINT "three"
mnext:
NEXT choice
x# = 0#
FOR t = 1 TO 10
    x# = x# + 1# / t
NEXT t
PRINT USING "harmonic ##.#####"; x#
END

Banner:
    PRINT STRING$(10, "=")
    RETURN

people:
DATA 6
DATA "Alice", 88, "Bob", 72, "Carol", 95
DATA "Dave", 61, "Eve", 79, "Frank", 50

SUB Report (nm$(1), sc(1), count%) STATIC
    PRINT "Rank Name     Score"
    FOR r% = 1 TO count%
        PRINT USING "###  \      \ ###"; r%; nm$(r%); sc(r%)
    NEXT r%
END SUB
