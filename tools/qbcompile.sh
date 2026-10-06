#!/bin/sh
# Compiles QuickBASIC 3.0 source files to BRUN30 executables with the original
# compiler running inside DOSBox Staging.
#
# usage: qbcompile.sh OUTDIR FILE.BAS...
#
# Every source file is compiled with "QB name,name OPTIONS;" and linked with
# "LINK name;". Compiler switches can be given in the source with a comment
# line of the form:   ' QBOPTS: /D /E
#
# For each input NAME.BAS the following files are written to OUTDIR:
#   NAME.EXE   the linked program (missing if compilation failed)
#   NAME.ERR   compiler output (errors, warnings)
#
# Environment:
#   QB30_DIR   directory containing QB.EXE, LINK.EXE and BRUN30.LIB
#   DOSBOX     path of the DOSBox Staging executable

set -e

QB30_DIR=${QB30_DIR:-/c/Users/Eric/Projects/quickbasic-3-disassembly/QB30}
DOSBOX=${DOSBOX:-"/c/Users/Eric/AppData/Local/Programs/DOSBox Staging/dosbox.exe"}

if [ $# -lt 2 ]; then
    echo "usage: $0 OUTDIR FILE.BAS..." >&2
    exit 2
fi

OUTDIR=$1
shift
mkdir -p "$OUTDIR"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/qbc.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

for f in QB.EXE LINK.EXE BRUN30.LIB BRUN30.EXE; do
    cp "$QB30_DIR/$f" "$WORK/"
done

BATCH="$WORK/BUILD.BAT"
: > "$BATCH"
NAMES=""
for src in "$@"; do
    base=$(basename "$src")
    name=$(echo "${base%.*}" | tr '[:lower:]' '[:upper:]' | cut -c1-8)
    # Convert to CRLF line endings; QB 3.0 misparses LF only files.
    sed -e 's/\r$//' -e 's/$/\r/' "$src" > "$WORK/$name.BAS"
    opts=$(sed -n "s/^[[:space:]]*'[[:space:]]*QBOPTS:[[:space:]]*\(.*\)\$/\1/p" "$src" | head -n 1 | tr -d '\r')
    printf 'QB %s,%s %s; > %s.ERR\r\n' "$name" "$name" "$opts" "$name" >> "$BATCH"
    printf 'IF EXIST %s.OBJ LINK %s; > %s.LNK\r\n' "$name" "$name" "$name" >> "$BATCH"
    NAMES="$NAMES $name"
done

WWORK=$(cygpath -w "$WORK" 2>/dev/null || echo "$WORK")
timeout 300 "$DOSBOX" --noprimaryconf --nolocalconf --set cycles=max --set startup_verbosity=quiet \
    -c "mount c \"$WWORK\"" -c "c:" -c "call BUILD.BAT" -c "exit" > "$WORK/dosbox.log" 2>&1 || true

status=0
for name in $NAMES; do
    rm -f "$OUTDIR/$name.EXE"
    cp "$WORK/$name.ERR" "$OUTDIR/" 2>/dev/null || true
    cp "$WORK/$name.LNK" "$OUTDIR/" 2>/dev/null || true
    if [ -f "$WORK/$name.EXE" ]; then
        cp "$WORK/$name.EXE" "$OUTDIR/"
    else
        echo "compile failed: $name" >&2
        status=1
    fi
done
exit $status
