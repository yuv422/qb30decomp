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

Programs that CHAIN to each other share a COMMON block. Decompile them
together so that what one program shows about the block helps with the others
(each `NAME.EXE` is written to `OUTDIR/NAME.BAS`):

```bash
qb30decomp -d OUTDIR GAME.EXE PART2.EXE PART3.EXE
```

Options: `--disasm` (annotated disassembly), `--dump-lifted` (lifted basic
blocks), `--info`, `--no-comments`, `-v` (show warnings).

The output uses CRLF line endings (QB 3.0 misparses LF only files).
Variables are named `variXXXX` after their DS address with a type suffix
(`%`, `#`, `$`, none for single). Labels are `locXXXX`, SUBs `subXXXX`,
DEF FN functions `FNfXXXX`. Programs compiled with /X, /E or /D carry a line
number table; their line numbers are restored (so ERL and TRON give the same
results) and used as label names. A `' QBOPTS:` comment lists the compiler
switches the program needs (/E /X /W /D).

The output keeps the program's memory layout where it can:
- The COMMON block is declared with every variable at its original offset;
  parts the program never uses get placeholder variables named by address.
- Static arrays are declared where QB allocated them, just before the first
  line that uses something allocated after them. Unused space after an array
  is reserved too.

QB 3.0 accepts lines of at most 252 characters, and the `variXXXX` names make
expressions much longer than in the original. Long ON GOTO/GOSUB, INPUT # and
PRINT lists are split into several statements. Parts of long expressions are
moved into temporaries named `tmpN` with a type suffix.

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
5. `postprocess` recovers array dimensions (bounded by the COMMON block, DEF FN
   parameters and what programs sharing the block show), variable types and
   inlines compiler temporaries.
6. `emitter` structures the CFG (IF/ELSEIF/ELSE, SELECT CASE, FOR/NEXT incl.
   variable STEP, DO WHILE / LOOP WHILE / EXIT, GOTO fallback) and prints the
   program.

## Tests

`tests/basic/*.BAS` exercise the language. `tests/run_tests.sh` compiles them
with QB 3.0 in DOSBox Staging (`tools/qbcompile.sh`), decompiles, recompiles
the output and checks the result is a fixpoint. `tests/roundtrip_exe.sh` does
the same for existing executables, and `tests/roundtrip_project.sh` for a set
of programs decompiled together with `-d`.

Status: all 35 tests pass; 156 of the 158 sample executables in the
quickbasic-3-disassembly repository round trip (CINT.EXE contains a patched
opcode, SUB1F1D.EXE writes past an undeclared array).

## Known limitations / TODO

- Static arrays only used with constant indices become scalars (same
  behaviour, different memory layout).
- WHILE ... WEND comes out as the equivalent DO WHILE ... LOOP; a SELECT CASE
  that has nothing but CASE ELSE comes out as an assignment to its hidden
  temporary followed by the CASE ELSE statements.
- Line numbers of programs compiled without /X, /E or /D are not stored in the
  executable; GOTO targets become `locXXXX` labels.
- Programs compiled with /O (BCOM30.LIB) are not supported.
- Recompiling a program built in the QB environment ("bz" format) with the
  command-line compiler can't reproduce it exactly. The command-line compiler
  puts string literals after the variables instead of between them, leaves out
  unreachable code, and only writes a line number table with /E or /X. A
  second decompile of such a program can therefore differ (adjacent arrays may
  merge into one), although it behaves the same.
