' Evaluation order: QB reorders + - chains ("a + b - c" is computed as
' "a - c + b"); the decompiled source must compile to the same order
DEF FNf (x) = x * 2 + 1
a = 1: b = 2: c = 3: d = 4: ee = 5
a% = 1: b% = 2: c% = 3: d% = 4: e% = 5
k = 1
IF k = 1 THEN k = 0
r1 = a + b - c
IF k = 2 THEN k = 0
r2 = (a + b) - c
IF k = 3 THEN k = 0
r3 = a + (b - c)
IF k = 4 THEN k = 0
r4 = a - c + b
IF k = 5 THEN k = 0
r5 = a + b * c - d
IF k = 6 THEN k = 0
r6 = a * b + c - d
IF k = 7 THEN k = 0
r7 = a + b + c - d
IF k = 8 THEN k = 0
r8 = a + b - c - d
IF k = 9 THEN k = 0
r9 = a + b - c + d - ee
IF k = 10 THEN k = 0
r10 = (a + b * c) - d
IF k = 11 THEN k = 0
r11 = a * b / c
IF k = 12 THEN k = 0
r12 = a / b * c
IF k = 13 THEN k = 0
r13 = a * b / (c + d)
IF k = 14 THEN k = 0
r14 = (a * b) / c
IF k = 15 THEN k = 0
r15 = a - b - c
IF k = 16 THEN k = 0
r16 = a - (b + c)
IF k = 17 THEN k = 0
r17 = a * b - c * d + ee * a
IF k = 18 THEN k = 0
r18 = a + b - c * d
IF k = 19 THEN k = 0
r19 = a + b * c - d * ee
IF k = 20 THEN k = 0
r20 = a + SQR(b) - c
IF k = 21 THEN k = 0
r21 = a * b * c / d
IF k = 22 THEN k = 0
r22 = a * b / c * d
IF k = 23 THEN k = 0
r23 = a / b / c
IF k = 24 THEN k = 0
r24 = -a + b - c
IF k = 25 THEN k = 0
r25 = a + b - (c - d)
IF k = 26 THEN k = 0
r26 = a + b + c + d
IF k = 27 THEN k = 0
r27 = a + b + c - d
IF k = 28 THEN k = 0
r28 = a + b - c + d
IF k = 29 THEN k = 0
r29 = a + b - c - d
IF k = 30 THEN k = 0
r30 = a - b + c + d
IF k = 31 THEN k = 0
r31 = a - b + c - d
IF k = 32 THEN k = 0
r32 = a - b - c + d
IF k = 33 THEN k = 0
r33 = a - b - c - d
IF k = 34 THEN k = 0
r34 = a + b + c + d + ee
IF k = 35 THEN k = 0
r35 = a + b + c + d - ee
IF k = 36 THEN k = 0
r36 = a + b + c - d + ee
IF k = 37 THEN k = 0
r37 = a + b + c - d - ee
IF k = 38 THEN k = 0
r38 = a + b - c + d + ee
IF k = 39 THEN k = 0
r39 = a + b - c + d - ee
IF k = 40 THEN k = 0
r40 = a + b - c - d + ee
IF k = 41 THEN k = 0
r41 = a + b - c - d - ee
IF k = 42 THEN k = 0
r42 = a - b + c + d + ee
IF k = 43 THEN k = 0
r43 = a - b + c + d - ee
IF k = 44 THEN k = 0
r44 = a - b + c - d + ee
IF k = 45 THEN k = 0
r45 = a - b + c - d - ee
IF k = 46 THEN k = 0
r46 = a - b - c + d + ee
IF k = 47 THEN k = 0
r47 = a - b - c + d - ee
IF k = 48 THEN k = 0
r48 = a - b - c - d + ee
IF k = 49 THEN k = 0
r49 = a - b - c - d - ee
IF k = 50 THEN k = 0
r50 = a + b * c - d
IF k = 51 THEN k = 0
r51 = a + b - c * d
IF k = 52 THEN k = 0
r52 = a * b + c - d
IF k = 53 THEN k = 0
r53 = a * b + c * d - ee
IF k = 54 THEN k = 0
r54 = a * b + c - d * ee
IF k = 55 THEN k = 0
r55 = a + b * c - d * ee
IF k = 56 THEN k = 0
r56 = a * b + c * d - ee * a
IF k = 57 THEN k = 0
r57 = a - b + c * d - ee
IF k = 58 THEN k = 0
r58 = a * b - c + d * ee - a
IF k = 59 THEN k = 0
r59 = a * b - c + d - ee
IF k = 60 THEN k = 0
r60 = a + b * c + d - ee
IF k = 61 THEN k = 0
r61 = a + b + c * d - ee
IF k = 62 THEN k = 0
r62 = a + b * c - d - ee
IF k = 63 THEN k = 0
r63 = b * c + a - d
IF k = 64 THEN k = 0
r64 = a - d * ee + b * c
IF k = 65 THEN k = 0
r65 = a - c * d + b
IF k = 66 THEN k = 0
r66 = SQR(b) + a - c
IF k = 67 THEN k = 0
r67 = a * b - SQR(c) + (c - d) / (ee + 1) - b / 7 + a / 3
IF k = 68 THEN k = 0
r68 = a * b - SQR(c) - b / 7 + (c - d) / (ee + 1) + a / 3
IF k = 69 THEN k = 0
r69 = a * b + (c - d) / (ee + 1) - SQR(c) + a / 3 - b / 7
IF k = 70 THEN k = 0
r70 = a + SQR(b) - c * d
IF k = 71 THEN k = 0
r71 = a + SQR(b) - SQR(c)
IF k = 72 THEN k = 0
r72 = a * b + SQR(c) - d
IF k = 73 THEN k = 0
r73 = a * b + c * d - ee * a * b
IF k = 74 THEN k = 0
r74 = a + b * c * d - ee * a
IF k = 75 THEN k = 0
r75 = ((a - d) - c) + b
IF k = 76 THEN k = 0
r76 = (a - c) + b
IF k = 77 THEN k = 0
r77 = ((b * c + a) - ee) + d
IF k = 78 THEN k = 0
r78 = ((c * d + a) - b) - ee
IF k = 79 THEN k = 0
r79 = ((a * b + d * ee) - c) - a
IF k = 80 THEN k = 0
r80 = (b * c + a) - d
IF k = 81 THEN k = 0
r81 = (SQR(b) + a) - c
IF k = 82 THEN k = 0
r82 = (((a * b - SQR(c)) + (c - d) / (ee + 1)) - b / 7) + a / 3
IF k = 83 THEN k = 0
r83 = ((a * b - SQR(c)) - b / 7) + (c - d) / (ee + 1)
IF k = 84 THEN k = 0
r84 = (a * c) / b
IF k = 85 THEN k = 0
r85 = ((a * b) * d) / c
IF k = 86 THEN k = 0
r86 = (a / b) * c
IF k = 87 THEN k = 0
r87 = (a / b) / c
IF k = 88 THEN k = 0
r88 = (a - c * d) + b
IF k = 89 THEN k = 0
r89 = (a * b - d) + c
IF k = 90 THEN k = 0
r90 = ((a + b) - d) + c
IF k = 91 THEN k = 0
r91 = (a + b) - d
IF k = 92 THEN k = 0
r92 = (a - b) - c
IF k = 93 THEN k = 0
r93 = (a * b - c) + d * ee
IF k = 94 THEN k = 0
r94 = (a - b) + c * d
IF k = 95 THEN k = 0
r95 = ((a - b) + c * d) - ee
IF k = 96 THEN k = 0
r96 = (a + b) * c - d
IF k = 97 THEN k = 0
r97 = a - (b + c)
IF k = 98 THEN k = 0
r98 = a - (b - c)
IF k = 99 THEN k = 0
r99 = (a - ee * a) + b * c * d
IF k = 100 THEN k = 0
r100 = a * b * c
IF k = 101 THEN k = 0
r101 = a * b * c * d
IF k = 102 THEN k = 0
r102 = a * b * c * d * ee
IF k = 103 THEN k = 0
r103 = a * b * c * d / ee
IF k = 104 THEN k = 0
r104 = a * b * c / d
IF k = 105 THEN k = 0
r105 = a * b * c / d * ee
IF k = 106 THEN k = 0
r106 = a * b * c / d / ee
IF k = 107 THEN k = 0
r107 = a * b / c
IF k = 108 THEN k = 0
r108 = a * b / c * d
IF k = 109 THEN k = 0
r109 = a * b / c * d * ee
IF k = 110 THEN k = 0
r110 = a * b / c * d / ee
IF k = 111 THEN k = 0
r111 = a * b / c / d
IF k = 112 THEN k = 0
r112 = a * b / c / d * ee
IF k = 113 THEN k = 0
r113 = a * b / c / d / ee
IF k = 114 THEN k = 0
r114 = a / b * c
IF k = 115 THEN k = 0
r115 = a / b * c * d
IF k = 116 THEN k = 0
r116 = a / b * c * d * ee
IF k = 117 THEN k = 0
r117 = a / b * c * d / ee
IF k = 118 THEN k = 0
r118 = a / b * c / d
IF k = 119 THEN k = 0
r119 = a / b * c / d * ee
IF k = 120 THEN k = 0
r120 = a / b * c / d / ee
IF k = 121 THEN k = 0
r121 = a / b / c
IF k = 122 THEN k = 0
r122 = a / b / c * d
IF k = 123 THEN k = 0
r123 = a / b / c * d * ee
IF k = 124 THEN k = 0
r124 = a / b / c * d / ee
IF k = 125 THEN k = 0
r125 = a / b / c / d
IF k = 126 THEN k = 0
r126 = a / b / c / d * ee
IF k = 127 THEN k = 0
r127 = a / b / c / d / ee
IF k = 128 THEN k = 0
r128 = a * (b + c) / d
IF k = 129 THEN k = 0
r129 = a / (b + c) * d
IF k = 130 THEN k = 0
r130 = (a + b) / c * d
IF k = 131 THEN k = 0
r131 = (a + b) * c / d
IF k = 132 THEN k = 0
r132 = a * b / (c + d) / ee
IF k = 133 THEN k = 0
r133 = SQR(a) / b * c
IF k = 134 THEN k = 0
r134 = a / SQR(b) * c
IF k = 135 THEN k = 0
r135 = a * SQR(b) / c
IF k = 136 THEN k = 0
r136 = a / b * SQR(c)
IF k = 137 THEN k = 0
r137 = (a + b) / (c + d) * ee
IF k = 138 THEN k = 0
r138 = a / (b + c) / (d + ee)
IF k = 139 THEN k = 0
r139 = a / b * c * d
i1% = a% + b% - c%
i2% = a% + b% - c% - d%
i3% = a% * b% + c% - d%
i4% = (a% + b%) - c%
i5% = a% - b% + c% * d% - e%
IF FNf(a) > 0 AND FNf(b) = 0 THEN PRINT "x"
IF FNf(b) = 0 AND FNf(a) > 0 THEN PRINT "y"
IF a > 0 AND FNf(b) = 0 THEN PRINT "z"
IF FNf(b) = 0 AND a > 0 THEN PRINT "w"
f1 = FNf(a) - FNf(b) + FNf(a + b)
f2 = a * FNf(b) - FNf(a)
PRINT r1; r2; i1%; i5%; f1; f2
