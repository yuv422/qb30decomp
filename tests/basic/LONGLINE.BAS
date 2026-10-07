' ROUNDTRIP: compile
' (the compiler reorders the long sum in DEF FNlong differently when recompiled)
' Lines that get too long with long variable names
DEF FNlong (alpha, beta) = alpha * beta + (alpha - beta) / (alpha + beta + 1) - SQR(ABS(alpha * alpha - beta * beta)) + alpha / 3 - beta / 7 + (alpha > beta) * 100 + (alpha < beta) * 200
DIM tbl(20, 20)
k = 3
ON k GOTO l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14, l15, l16, l17, l18, l19, l20, l21, l22, l23, l24, l25, l26, l27, l28, l29, l30, l31, l32, l33, l34, l35, l36, l37, l38, l39, l40
l1: PRINT 1: GOTO done
l2: PRINT 2: GOTO done
l3: PRINT 3: GOTO done
l4: PRINT 4: GOTO done
l5: PRINT 5: GOTO done
l6: PRINT 6: GOTO done
l7: PRINT 7: GOTO done
l8: PRINT 8: GOTO done
l9: PRINT 9: GOTO done
l10: PRINT 10: GOTO done
l11: PRINT 11: GOTO done
l12: PRINT 12: GOTO done
l13: PRINT 13: GOTO done
l14: PRINT 14: GOTO done
l15: PRINT 15: GOTO done
l16: PRINT 16: GOTO done
l17: PRINT 17: GOTO done
l18: PRINT 18: GOTO done
l19: PRINT 19: GOTO done
l20: PRINT 20: GOTO done
l21: PRINT 21: GOTO done
l22: PRINT 22: GOTO done
l23: PRINT 23: GOTO done
l24: PRINT 24: GOTO done
l25: PRINT 25: GOTO done
l26: PRINT 26: GOTO done
l27: PRINT 27: GOTO done
l28: PRINT 28: GOTO done
l29: PRINT 29: GOTO done
l30: PRINT 30: GOTO done
l31: PRINT 31: GOTO done
l32: PRINT 32: GOTO done
l33: PRINT 33: GOTO done
l34: PRINT 34: GOTO done
l35: PRINT 35: GOTO done
l36: PRINT 36: GOTO done
l37: PRINT 37: GOTO done
l38: PRINT 38: GOTO done
l39: PRINT 39: GOTO done
l40: PRINT 40: GOTO done
done:
ON k GOSUB s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30
a = 1: b = 2: c = 3: d = 4: e = 5
x = (a + b) * (c - d) / (e + 1) + tbl(a + 1, b + 2) * tbl(c + 3, d + 4) - tbl(e, a) / (tbl(b, c) + 1) + tbl(a + 1, b + 2) * tbl(c + 3, d + 4)
IF tbl(a + 1, b + 2) * tbl(c + 3, d + 4) > tbl(e, a) / (tbl(b, c) + 1) AND tbl(a + 1, b + 2) < 100 OR tbl(c + 3, d + 4) = tbl(b, c) AND x > 2 THEN PRINT "yes"
PRINT FNlong(a, b); FNlong(c, d)
OPEN "LONG.DAT" FOR OUTPUT AS #1
WRITE #1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
CLOSE #1
OPEN "LONG.DAT" FOR INPUT AS #1
INPUT #1, tbl(1, 1), tbl(1, 2), tbl(1, 3), tbl(2, 1), tbl(2, 2), tbl(2, 3), tbl(3, 1), tbl(3, 2), tbl(3, 3), tbl(4, 1), tbl(4, 2), tbl(4, 3)
CLOSE #1
KILL "LONG.DAT"
IF k <> 99 THEN GOTO fin
hang:
GOTO hang
fin:
END
s1: PRINT "s1": RETURN
s2: PRINT "s2": RETURN
s3: PRINT "s3": RETURN
s4: PRINT "s4": RETURN
s5: PRINT "s5": RETURN
s6: PRINT "s6": RETURN
s7: PRINT "s7": RETURN
s8: PRINT "s8": RETURN
s9: PRINT "s9": RETURN
s10: PRINT "s10": RETURN
s11: PRINT "s11": RETURN
s12: PRINT "s12": RETURN
s13: PRINT "s13": RETURN
s14: PRINT "s14": RETURN
s15: PRINT "s15": RETURN
s16: PRINT "s16": RETURN
s17: PRINT "s17": RETURN
s18: PRINT "s18": RETURN
s19: PRINT "s19": RETURN
s20: PRINT "s20": RETURN
s21: PRINT "s21": RETURN
s22: PRINT "s22": RETURN
s23: PRINT "s23": RETURN
s24: PRINT "s24": RETURN
s25: PRINT "s25": RETURN
s26: PRINT "s26": RETURN
s27: PRINT "s27": RETURN
s28: PRINT "s28": RETURN
s29: PRINT "s29": RETURN
s30: PRINT "s30": RETURN
