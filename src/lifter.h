// Lifting of machine code to BASIC statements by symbolic execution.
//
// Registers, the floating point accumulator (FAC, DS:16/DS:1A), the numeric
// temp slots, the BP frame spill area and the stack hold symbolic values. The
// QB compiler caches values in registers and temp slots across statements and
// even across basic blocks, so the state is propagated along the control flow
// graph (with a fixpoint over joins) instead of being reset per statement.
#pragma once

#include "cfg.h"
#include "ir.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace qb {

// Information about a DEF FN function.
struct FnInfo {
    Ty type = Ty::Unknown;            // return type
    std::vector<uint16_t> params;     // parameter variables (DS addresses), in order
    std::vector<Ty> paramTypes;
    uint16_t resultVar = 0;           // multi-line: variable holding the result
    bool hasResultVar = false;
    ExprPtr body;                     // single line: expression
};

struct SubParam {
    Ty type = Ty::Unknown;
    bool isArray = false;
    int dims = 1;
};

struct SubInfo {
    std::vector<SubParam> params;
    std::string name;
};

// A conditional/unconditional branch in a lifted block.
struct LiftedBlock {
    std::vector<Stmt> stmts;
    ExprPtr cond;                 // Cond terminator: condition for taking succs[0]
    ExprPtr selector;             // OnGoto terminator
    bool returnWithTarget = false; // "RETURN line": Jump terminator is a RETURN to succs[0]
    bool noJump = false;          // the Jump terminator is implied by the last statement (RUN)
    bool reached = false;
};

// Facts collected over the whole program (filled by the first pass, used by the second).
struct GlobalInfo {
    std::map<uint16_t, FnInfo> fns;
    std::map<uint16_t, SubInfo> subs;
    std::map<uint16_t, Ty> dynArrays;     // descriptor -> element type
    std::map<uint16_t, int> dynDims;      // descriptor -> number of dimensions
    std::set<uint16_t> dataKeys;          // DATA statement keys (code addresses)
    std::set<uint16_t> restoreTargets;    // keys used by RESTORE n
    // first pass observations
    std::map<uint16_t, std::vector<std::vector<uint16_t>>> fnCallStores; // fn -> per call: vars stored before
    std::map<uint16_t, std::set<uint16_t>> fnBodyWrites;  // fn -> vars written in its body
    std::map<uint16_t, std::set<uint16_t>> fnBodyReads;   // fn -> vars read in its body
    std::map<uint16_t, uint16_t> fnResultReads;           // fn -> first var read after call
    std::map<uint16_t, Ty> fnResultTypes;
    std::map<uint16_t, std::set<uint32_t>> varReadProcs;  // var -> procs (index) reading it
    std::map<uint16_t, std::set<uint32_t>> varWriteProcs; // var -> procs (index) writing it
    std::map<uint16_t, std::set<Ty>> varTypes;            // var -> types it was accessed with
    std::map<uint16_t, std::vector<std::vector<ExprPtr>>> subCallArgs; // sub -> args per call
    std::map<uint16_t, uint16_t> arrayBytes;  // static array base -> size in bytes (ERASE, GET)
    std::map<uint16_t, Ty> staticArrays;      // static arrays seen in ERASE
    std::map<uint16_t, uint32_t> arrayExtent; // static array base -> estimated size (after pass 1)
};

class Lifter {
public:
    Lifter(Program& program, GlobalInfo& info);

    // pass 1 gathers information, pass 2 produces the final statements
    void run(int pass);

    // results, indexed like Program::procs() / Proc::blocks
    const std::vector<std::vector<LiftedBlock>>& results() const { return results_; }
    std::vector<std::vector<LiftedBlock>>& results() { return results_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    struct Impl;

private:
    Program& program_;
    GlobalInfo& info_;
    std::vector<std::vector<LiftedBlock>> results_;
    std::vector<std::string> warnings_;
};

} // namespace qb
