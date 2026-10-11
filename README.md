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
switches the program was compiled with (/D /E /Q /R /S /V /W /X, read from the
executable's header; /Q is the undocumented "optimise for speed" switch).
Unreachable code gets a label: the compiler drops statements that follow
GOTO, RETURN or END unless a label or line number starts them. With /R (arrays in row order) subscripts are printed in
source order.

The output keeps the program's memory layout where it can:
- The COMMON block is declared with every variable at its original offset;
  parts the program never uses get placeholder variables named by address.
- Programs that use BLOAD, BSAVE, PEEK, POKE or CALL ABSOLUTE start with
  `DEF SEG`: QB keeps the DEF SEG segment across CHAIN, and a rebuilt program
  may have its data somewhere else than the one that chains to it.
- Static arrays are declared where QB allocated them, just before the first
  line that uses something allocated after them. Unused space after an array
  is reserved too.

QB 3.0 accepts lines of at most 252 characters, and the `variXXXX` names make
expressions much longer than in the original. A longer statement is continued
on the next line with ` _` (QB allows this wherever a space may be, but not in
a string). DATA can't be continued, so a long DATA line becomes several DATA
statements.

Expressions are printed so that QB compiles them in the order the original code
evaluates them (floating point results depend on it). QB reorders as it
compiles: "a + b - c" becomes "a - c + b" and "a / b / c" becomes
"a / (b * c)". So an addition followed by a subtraction gets parentheses,
"(a + b) - c", and a product used as a divisor is written as successive
divisions. The EVALORD test covers this.

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

Status: all 39 tests pass; 156 of the 158 sample executables in the
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
- Recompiling a program built in the QB environment ("bz" format) with
  `QB name,name;` + LINK can't reproduce its memory layout: the command-line
  compiler puts string literals after the variables instead of between them.
  A second decompile can therefore differ (adjacent arrays may merge into
  one), although it behaves the same. The undocumented `QB name /F [options]`
  switch compiles straight to a bz executable like the environment does
  (literals between the variables, no OBJ or LINK step). It differs from an
  environment build only in header bit 2Eh/80h and in the line number
  table: the environment writes one for every program with line numbers,
  /F only with /E, /X or /D.
