// Disassembly of the compiled BASIC code segment.
//
// x86 instructions are decoded with Zydis. "INT 3Dh/3Eh/3Fh" runtime calls are
// fused with their opcode byte and inline operands into a single instruction.
#pragma once

#include "exe_loader.h"

#include <Zydis/Zydis.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace qb {

struct RtCall {
    int intNo = 0;                 // 0x3d, 0x3e, 0x3f
    int op = 0;                    // opcode byte
    std::vector<uint8_t> bytes;    // inline operand bytes
    std::vector<uint16_t> targets; // ON GOTO / ON GOSUB jump table, GOSUB target
    uint16_t word = 0;             // inline word operand

    bool is(int i, int o) const { return intNo == i && op == o; }
    int byteArg(size_t i = 0) const { return i < bytes.size() ? bytes[i] : 0; }
};

struct Insn {
    uint16_t addr = 0;
    uint8_t length = 0;
    bool isRt = false;
    bool isEventCheck = false;     // INT 3 (0xCC) emitted for event trapping
    RtCall rt;
    ZydisDecodedInstruction zi{};
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT]{};

    uint16_t next() const { return uint16_t(addr + length); }
    ZydisMnemonic mnem() const { return zi.mnemonic; }
    bool isJmp() const { return !isRt && zi.mnemonic == ZYDIS_MNEMONIC_JMP; }
    bool isJcc() const;
    bool isCall() const { return !isRt && zi.mnemonic == ZYDIS_MNEMONIC_CALL; }
    bool isRet() const { return !isRt && zi.mnemonic == ZYDIS_MNEMONIC_RET && (zi.opcode == 0xC2 || zi.opcode == 0xC3); }
    bool isRetf() const { return !isRt && zi.mnemonic == ZYDIS_MNEMONIC_RET && (zi.opcode == 0xCA || zi.opcode == 0xCB); }
    // Target of a relative JMP/Jcc/CALL. Returns false if not a direct branch.
    bool branchTarget(uint16_t& target) const;
    // Target of "CALL FAR 0000:offset" (a SUB in a linked program).
    bool farCallTarget(uint16_t& target) const;
};

class Disassembly {
public:
    Disassembly(const LoadedProgram& prog);

    // Decodes all code reachable from the program entry and from the roots found
    // while decoding (GOSUB/SUB/DEF FN targets, ON ... GOTO/GOSUB tables, trap
    // handlers), then fills gaps of unreachable code.
    void run();

    const std::map<uint16_t, Insn>& insns() const { return insns_; }
    const Insn* at(uint16_t addr) const;
    const LoadedProgram& program() const { return prog_; }

    // Targets of near CALLs (GOSUB, DEF FN, SUB).
    const std::set<uint16_t>& callTargets() const { return callTargets_; }
    // Targets of far CALLs into the code segment (SUBs in "bm" programs).
    const std::set<uint16_t>& farCallTargets() const { return farCallTargets_; }
    // Addresses that are referenced as labels (jump/call targets, handlers ...).
    const std::set<uint16_t>& labels() const { return labels_; }
    // Handlers installed with ON ERROR / ON KEY ... / RESUME label targets.
    const std::set<uint16_t>& trapTargets() const { return trapTargets_; }

    std::string format(const Insn& insn) const;
    void dump(FILE* out) const;

private:
    bool decode(uint16_t addr, Insn& out) const;
    void explore(uint16_t start);

    const LoadedProgram& prog_;
    ZydisDecoder decoder_;
    ZydisFormatter formatter_;
    std::map<uint16_t, Insn> insns_;
    std::set<uint16_t> callTargets_;
    std::set<uint16_t> farCallTargets_;
    std::set<uint16_t> labels_;
    std::set<uint16_t> trapTargets_;
    std::vector<uint16_t> work_;
};

} // namespace qb
