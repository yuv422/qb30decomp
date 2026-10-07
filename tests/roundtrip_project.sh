#!/bin/sh
# Round trip test for a set of programs that CHAIN to each other (and share a
# COMMON block). They are decompiled together (-d), compiled, decompiled
# together again and compared.
#
# usage: tests/roundtrip_project.sh OUTDIR PROGRAM.EXE...

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DECOMP=${DECOMP:-$ROOT/build/qb30decomp.exe}
OUT=$1
shift
mkdir -p "$OUT/dec1" "$OUT/re" "$OUT/dec2"
rm -f "$OUT"/dec1/*.BAS "$OUT"/re/*.EXE "$OUT"/dec2/*.BAS

normalize() {
    perl "$ROOT/tools/normalize.pl" "$1"
}

"$DECOMP" -d "$OUT/dec1" "$@"
"$ROOT/tools/qbcompile.sh" "$OUT/re" "$OUT"/dec1/*.BAS >/dev/null 2>&1
"$DECOMP" -d "$OUT/dec2" "$OUT"/re/*.EXE 2>/dev/null

pass=0
fail=0
for f in "$OUT"/dec1/*.BAS; do
    name=$(basename "$f" .BAS)
    if [ ! -f "$OUT/re/$name.EXE" ]; then
        echo "FAIL $name: decompiled source does not compile"
        sed 's/^/    /' "$OUT/re/$name.ERR" | head -12
        fail=$((fail + 1))
        continue
    fi
    normalize "$f" > "$OUT/dec1/$name.norm"
    normalize "$OUT/dec2/$name.BAS" > "$OUT/dec2/$name.norm"
    if diff -q "$OUT/dec1/$name.norm" "$OUT/dec2/$name.norm" >/dev/null; then
        echo "ok   $name"
        pass=$((pass + 1))
    else
        echo "DIFF $name: decompiling the recompiled program gives a different result"
        diff "$OUT/dec1/$name.norm" "$OUT/dec2/$name.norm" | head -12 | sed 's/^/    /'
        fail=$((fail + 1))
    fi
done
echo "== $pass passed, $fail failed"
[ $fail -eq 0 ]
