// Procedures and control flow graphs.
//
// The code segment is split into procedures (main program, SUBs, DEF FN
// functions). Each procedure is a list of basic blocks in address order.
// A few multi-instruction idioms emitted by the compiler (boolean results,
// integer ABS/SGN, WAIT loops) are fused into single pseudo instructions so
// that they don't split basic blocks.
#pragma once

#include "disasm.h"
#include "ir.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace qb {

enum class IdiomKind : uint8_t {
    None,
    SetBool,  // mov r,0 (earlier) / jcc +1 / dec r      -> r = cond
    AbsInt,   // and r,r / jnl +2 / neg r                 -> r = ABS(r)
    SgnInt,   // and r,r / jz L / mov r,1 / jnl L / neg r -> r = SGN(r)
    Wait,     // in al,dx / [xor al,bl] / and al,cl / jz self -> WAIT dx, cx [, bx]
};

// One element of a basic block: an instruction or a fused idiom.
struct LInsn {
    const Insn* insn = nullptr;        // first instruction
    IdiomKind idiom = IdiomKind::None;
    ZydisRegister reg = ZYDIS_REGISTER_NONE; // register affected by the idiom
    ZydisMnemonic cc = ZYDIS_MNEMONIC_INVALID; // SetBool: the jcc that skips the dec
    bool waitXor = false;              // Wait: uses the XOR mask (BX)
    uint16_t addr = 0;
    uint16_t end = 0;                  // address after the idiom / instruction
};

enum class ProcKind : uint8_t { Main, Sub, DefFn, DefFnSingle };

enum class TermKind : uint8_t {
    Fall,      // falls through to succs[0]
    Jump,      // unconditional jump to succs[0]
    Cond,      // conditional jump: succs[0] = taken, succs[1] = fall through
    OnGoto,    // ON n GOTO: succs = targets..., last = fall through
    Ret,       // RET / RETF / SUB_END / DEF_FN_END / RETURN
    Stop,      // END, STOP, RESUME, ... (no successor)
};

struct Block {
    uint32_t id = 0;
    uint16_t start = 0, end = 0;
    std::vector<LInsn> code;
    TermKind term = TermKind::Fall;
    std::vector<uint32_t> succs;
    std::vector<uint32_t> preds;
    bool isLabel = false;    // must keep a label (jump/call/trap target)
};

struct Proc {
    ProcKind kind = ProcKind::Main;
    uint16_t entry = 0;
    std::string name;
    std::vector<Block> blocks;           // address order
    std::map<uint16_t, uint32_t> blockAt; // start address -> block index
    std::set<uint16_t> gosubTargets;     // GOSUB / ON GOSUB / event handler targets inside
    int retfBytes = -1;                  // SUB: bytes of arguments removed by RETF n
    uint16_t lowAddr = 0xffff, highAddr = 0;

    const Block* blockFor(uint16_t addr) const;
};

class Program {
public:
    Program(const LoadedProgram& prog, const Disassembly& dis);

    const LoadedProgram& prog() const { return prog_; }
    const Disassembly& dis() const { return dis_; }
    std::vector<Proc>& procs() { return procs_; }
    const std::vector<Proc>& procs() const { return procs_; }

    // Near call targets classified as DEF FN / SUB / GOSUB.
    bool isFn(uint16_t a) const { return fns_.count(a) != 0; }
    bool isSub(uint16_t a) const { return subs_.count(a) != 0; }
    const Proc* procAt(uint16_t entry) const;
    // External far calls: address -> name (ABSOLUTE ...)
    std::string externalName(const Insn& call) const;

private:
    void classify();
    void buildProc(Proc& p, std::set<uint16_t> roots, const std::set<uint16_t>* claimed);
    std::vector<LInsn> linearize(uint16_t start, const std::set<uint16_t>& owned) const;

    const LoadedProgram& prog_;
    const Disassembly& dis_;
    std::set<uint16_t> fns_, subs_;
    std::vector<Proc> procs_;
};

} // namespace qb
