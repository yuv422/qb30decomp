#!/bin/sh
# Builds qb30decomp with MSVC (via build.bat) from a POSIX shell.
cmd //c "$(cygpath -w "$(dirname "$0")")\build.bat"
