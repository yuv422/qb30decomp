// Tables describing the BRUN30 runtime opcodes.
//
// Compiled BASIC code calls the runtime with "INT 3Dh/3Eh/3Fh" followed by an
// opcode byte. Some opcodes carry extra inline operand bytes after the opcode.
#pragma once

#include <cstdint>

namespace qb {

enum class InlineOperand {
    None,
    Byte,        // one byte (temp slot, dimension count, flags, power of two)
    Word,        // one word (GOSUB target, far call table offset)
    JumpTable,   // count byte followed by count words (ON GOTO / ON GOSUB)
    InputTypes,  // count byte followed by count type bytes (INPUT arguments)
    ArrayLayout, // element size byte, dims*2 byte, dims words (/D static array offset)
};

struct OpInfo {
    const char* name;
    InlineOperand operand;
};

// intNo is 0x3d, 0x3e or 0x3f.
const OpInfo& opInfo(int intNo, int op);

// Ends the flow of control (END, END PROGRAM, STOP ...).
bool opIsTerminal(int intNo, int op);

} // namespace qb
