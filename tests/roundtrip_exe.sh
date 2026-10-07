#!/bin/sh
# Round trip test for existing executables (for example programs built with
# the QB environment, "bz" format):
#   decompile -> compile the BASIC with QB 3.0 -> decompile again -> compare
#
# usage: tests/roundtrip_exe.sh OUTDIR PROGRAM.EXE...

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DECOMP=${DECOMP:-$ROOT/build/qb30decomp.exe}
OUT=$1
shift
rm -rf "$OUT"
mkdir -p "$OUT/dec1" "$OUT/re" "$OUT/dec2"

normalize() {
    perl "$ROOT/tools/normalize.pl" "$1"
}

names=""
for f in "$@"; do
    name=$(basename "$f")
    name=$(echo "${name%.*}" | tr '[:lower:]' '[:upper:]')
    if "$DECOMP" "$f" -o "$OUT/dec1/$name.BAS" 2>"$OUT/dec1/$name.log"; then
        names="$names $name"
    else
        echo "SKIP $name: $(head -1 "$OUT/dec1/$name.log")"
        rm -f "$OUT/dec1/$name.BAS"
    fi
done

"$ROOT/tools/qbcompile.sh" "$OUT/re" "$OUT"/dec1/*.BAS >/dev/null 2>&1

pass=0
fail=0
for name in $names; do
    if [ ! -f "$OUT/re/$name.EXE" ]; then
        echo "FAIL $name: decompiled source does not compile"
        sed 's/^/    /' "$OUT/re/$name.ERR" | head -12
        fail=$((fail + 1))
        continue
    fi
    "$DECOMP" "$OUT/re/$name.EXE" -o "$OUT/dec2/$name.BAS" 2>/dev/null
    normalize "$OUT/dec1/$name.BAS" > "$OUT/dec1/$name.norm"
    normalize "$OUT/dec2/$name.BAS" > "$OUT/dec2/$name.norm"
    if diff -q "$OUT/dec1/$name.norm" "$OUT/dec2/$name.norm" >/dev/null; then
        pass=$((pass + 1))
    else
        echo "FAIL $name: decompiling the recompiled program gives a different result"
        diff "$OUT/dec1/$name.norm" "$OUT/dec2/$name.norm" | head -12 | sed 's/^/    /'
        fail=$((fail + 1))
    fi
done
echo "== $pass passed, $fail failed"
[ $fail -eq 0 ]
