#!/bin/sh
# Round trip test of the decompiler.
#
#   1. compile every tests/basic/*.BAS with QuickBASIC 3.0 (in DOSBox)
#   2. decompile the executables
#   3. compile the decompiled sources again (must compile without errors)
#   4. decompile the new executables and compare with step 2 after
#      normalising variable / label names (the decompiled program must be a
#      fixpoint of compile + decompile)
#
# usage: tests/run_tests.sh [test names...]
#
# Environment: QB30_DIR, DOSBOX (see tools/qbcompile.sh), DECOMP (decompiler
# executable, default build/qb30decomp.exe)

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DECOMP=${DECOMP:-$ROOT/build/qb30decomp.exe}
OUT=$ROOT/build/roundtrip
SRC=$ROOT/tests/basic

rm -rf "$OUT"
mkdir -p "$OUT/orig" "$OUT/dec1" "$OUT/re" "$OUT/dec2"

if [ $# -gt 0 ]; then
    files=""
    for n in "$@"; do files="$files $SRC/$n.BAS"; done
else
    files=$(ls "$SRC"/*.BAS)
fi

echo "== compiling originals"
"$ROOT/tools/qbcompile.sh" "$OUT/orig" $files >/dev/null 2>&1

normalize() {
    # rename variables, labels, functions and SUBs by order of first appearance
    perl -ne '
        BEGIN { %m = (); %n = (); }
        next if /^\x27/;
        s/\r//;
        s{\b(vari|loc|FNf|sub)([0-9A-F]{4})\b}{
            my $k = "$1$2";
            $m{$k} //= $1 . (++$n{$1});
            $m{$k}
        }ge;
        print;
    ' "$1"
}

pass=0
fail=0
for f in $files; do
    name=$(basename "$f" .BAS)
    if [ ! -f "$OUT/orig/$name.EXE" ]; then
        echo "FAIL $name: original does not compile"
        fail=$((fail + 1))
        continue
    fi
    "$DECOMP" "$OUT/orig/$name.EXE" -o "$OUT/dec1/$name.BAS" 2>"$OUT/dec1/$name.log"
done

echo "== compiling decompiled sources"
"$ROOT/tools/qbcompile.sh" "$OUT/re" "$OUT"/dec1/*.BAS >/dev/null 2>&1

for f in $files; do
    name=$(basename "$f" .BAS)
    [ -f "$OUT/orig/$name.EXE" ] || continue
    if [ ! -f "$OUT/re/$name.EXE" ]; then
        echo "FAIL $name: decompiled source does not compile"
        sed 's/^/    /' "$OUT/re/$name.ERR" | head -20
        fail=$((fail + 1))
        continue
    fi
    "$DECOMP" "$OUT/re/$name.EXE" -o "$OUT/dec2/$name.BAS" 2>"$OUT/dec2/$name.log"
    normalize "$OUT/dec1/$name.BAS" > "$OUT/dec1/$name.norm"
    normalize "$OUT/dec2/$name.BAS" > "$OUT/dec2/$name.norm"
    if diff -q "$OUT/dec1/$name.norm" "$OUT/dec2/$name.norm" >/dev/null; then
        w=$(cat "$OUT/dec1/$name.log")
        if [ -n "$w" ]; then
            echo "ok   $name ($w)"
        else
            echo "ok   $name"
        fi
        pass=$((pass + 1))
    else
        echo "FAIL $name: decompiling the recompiled program gives a different result"
        diff "$OUT/dec1/$name.norm" "$OUT/dec2/$name.norm" | head -20 | sed 's/^/    /'
        fail=$((fail + 1))
    fi
done
echo "== $pass passed, $fail failed"
[ $fail -eq 0 ]
