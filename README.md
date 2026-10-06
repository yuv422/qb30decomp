# qb30decomp

Decompiler for QuickBASIC 3.0 programs compiled for the BRUN30 runtime.
It turns an EXE back into BASIC source that compiles with the original QB 3.0
compiler.

Both executable layouts are supported:
- "bz": written directly by the QB environment
- "bm": compiled to an OBJ and linked with BRUN30.LIB (`QB prog,prog;` + `LINK prog;`)

## Building

C++17 and CMake. Zydis is fetched automatically (FetchContent).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On this machine `build.bat` / `build.sh` set up MSVC and Ninja.

## Usage

```bash
qb30decomp PROGRAM.EXE -o PROGRAM.BAS
```

Options: `--disasm` (annotated disassembly), `--dump-lifted` (lifted basic
blocks), `--info`, `--no-comments`, `-v` (show warnings).

The output uses CRLF line endings (QB 3.0 misparses LF only files).
Variables are named `variXXXX` after their DS address with a type suffix
(`%`, `#`, `$`, none for single). Labels are `locXXXX`, SUBs `subXXXX`,
DEF FN functions `FNfXXXX`. A `' QBOPTS:` comment lists the compiler
switches the program needs (/E /X /W /D).

## How it works

1. `exe_loader` parses the EXE (bz records / bm segments), the initialised
   data (literals) and the DATA statements.
2. `disasm` decodes code with Zydis; `INT 3Dh/3Eh/3Fh` runtime calls are fused
   with their opcode and inline operands.
3. `cfg` splits the code into procedures (main, SUBs, DEF FNs) and basic
   blocks, fusing idioms (boolean results, integer ABS/SGN, WAIT loops).
4. `lifter*` symbolically executes registers, the floating accumulator,
   temp slots, BP spills and the stack with a fixpoint over the CFG (the
   compiler caches values across statements and does CSE), producing typed
   statements. Two passes: the first gathers DEF FN parameters, SUB parameter
   types and array information.
5. `postprocess` recovers array dimensions, variable types and inlines
   compiler temporaries.
6. `emitter` structures the CFG (IF/ELSEIF/ELSE, FOR/NEXT incl. variable STEP,
   DO WHILE / LOOP WHILE / EXIT, GOTO fallback) and prints the program.

## Tests

`tests/basic/*.BAS` exercise the language. `tests/run_tests.sh` compiles them
with QB 3.0 in DOSBox Staging (`tools/qbcompile.sh`), decompiles, recompiles
the output and checks the result is a fixpoint. `tests/roundtrip_exe.sh` does
the same for existing executables.

Status: 27 of 29 tests pass; 155 of the sample executables in the
quickbasic-3-disassembly repository round trip.

## Known limitations / TODO

- `$DYNAMIC` arrays passed to SUBs (descriptor copied with `REP MOVSW`) and
  multi-dimensional array parameters are not decoded (DYN3 test).
- `/D` builds: TRON/TROFF are only emitted when /D is detected (DEBUG test
  still differs).
- SELECT CASE comes out as IF/ELSE chains; static arrays only used with
  constant indices become scalars; line numbers (for ERL) are not restored.
- Programs compiled with /O (BCOM30.LIB) are not supported.
