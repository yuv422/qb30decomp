#include "disasm.h"

#include "opcodes.h"

#include <cstdio>
#include <cstring>

namespace qb {

bool Insn::isJcc() const {
    if (isRt) return false;
    switch (zi.mnemonic) {
    case ZYDIS_MNEMONIC_JB: case ZYDIS_MNEMONIC_JBE: case ZYDIS_MNEMONIC_JCXZ: case ZYDIS_MNEMONIC_JL:
    case ZYDIS_MNEMONIC_JLE: case ZYDIS_MNEMONIC_JNB: case ZYDIS_MNEMONIC_JNBE: case ZYDIS_MNEMONIC_JNL:
    case ZYDIS_MNEMONIC_JNLE: case ZYDIS_MNEMONIC_JNO: case ZYDIS_MNEMONIC_JNP: case ZYDIS_MNEMONIC_JNS:
    case ZYDIS_MNEMONIC_JNZ: case ZYDIS_MNEMONIC_JO: case ZYDIS_MNEMONIC_JP: case ZYDIS_MNEMONIC_JS:
    case ZYDIS_MNEMONIC_JZ: case ZYDIS_MNEMONIC_LOOP: case ZYDIS_MNEMONIC_LOOPE: case ZYDIS_MNEMONIC_LOOPNE:
        return true;
    default:
        return false;
    }
}

bool Insn::branchTarget(uint16_t& target) const {
    if (isRt) return false;
    if (!(isJmp() || isJcc() || isCall())) return false;
    if (zi.operand_count_visible < 1) return false;
    const ZydisDecodedOperand& op = ops[0];
    if (op.type != ZYDIS_OPERAND_TYPE_IMMEDIATE || !op.imm.is_relative) return false;
    target = uint16_t(addr + length + op.imm.value.s);
    return true;
}

bool Insn::farCallTarget(uint16_t& target) const {
    if (isRt || zi.mnemonic != ZYDIS_MNEMONIC_CALL || zi.opcode != 0x9A) return false;
    const ZydisDecodedOperand& op = ops[0];
    if (op.type != ZYDIS_OPERAND_TYPE_POINTER) return false;
    // SUBs of a linked ("bm") program live in the program's own code segment,
    // which is the first segment of the load module (segment 0 before relocation).
    if (op.ptr.segment != 0) return false;
    target = op.ptr.offset;
    return true;
}

Disassembly::Disassembly(const LoadedProgram& prog) : prog_(prog) {
    ZydisDecoderInit(&decoder_, ZYDIS_MACHINE_MODE_REAL_16, ZYDIS_STACK_WIDTH_16);
    ZydisFormatterInit(&formatter_, ZYDIS_FORMATTER_STYLE_INTEL);
}

const Insn* Disassembly::at(uint16_t addr) const {
    auto it = insns_.find(addr);
    return it == insns_.end() ? nullptr : &it->second;
}

bool Disassembly::decode(uint16_t addr, Insn& out) const {
    if (addr >= prog_.codeEnd) return false;
    const uint8_t* p = prog_.code.data() + addr;
    size_t avail = prog_.codeEnd - addr;
    out = Insn();
    out.addr = addr;

    if (p[0] == 0xCD && avail >= 3 && (p[1] == 0x3d || p[1] == 0x3e || p[1] == 0x3f)) {
        out.isRt = true;
        out.rt.intNo = p[1];
        out.rt.op = p[2];
        size_t pos = 3 + size_t(prog_.opcodePadding());
        const OpInfo& info = opInfo(p[1], p[2]);
        auto need = [&](size_t n) { return pos + n <= avail; };
        switch (info.operand) {
        case InlineOperand::None: break;
        case InlineOperand::Byte:
            if (!need(1)) return false;
            out.rt.bytes.push_back(p[pos++]);
            break;
        case InlineOperand::Word:
            if (!need(2)) return false;
            out.rt.word = uint16_t(p[pos] | (p[pos + 1] << 8));
            pos += 2;
            break;
        case InlineOperand::JumpTable: {
            if (!need(1)) return false;
            int n = p[pos++];
            out.rt.bytes.push_back(uint8_t(n));
            if (!need(size_t(n) * 2)) return false;
            for (int i = 0; i < n; ++i, pos += 2) out.rt.targets.push_back(uint16_t(p[pos] | (p[pos + 1] << 8)));
            break;
        }
        case InlineOperand::InputTypes: {
            if (!need(1)) return false;
            int n = p[pos++];
            out.rt.bytes.push_back(uint8_t(n));
            if (!need(size_t(n))) return false;
            for (int i = 0; i < n; ++i) out.rt.bytes.push_back(p[pos++]);
            break;
        }
        case InlineOperand::ArrayLayout: {
            if (!need(2)) return false;
            int elemSize = p[pos++];
            int dimBytes = p[pos++];
            out.rt.bytes.push_back(uint8_t(elemSize));
            out.rt.bytes.push_back(uint8_t(dimBytes));
            if (!need(size_t(dimBytes))) return false;
            for (int i = 0; i < dimBytes; ++i) out.rt.bytes.push_back(p[pos++]);
            break;
        }
        }
        if (out.rt.is(0x3f, 0x58)) out.rt.targets.push_back(out.rt.word);
        out.length = uint8_t(pos);
        return true;
    }

    ZyanStatus st = ZydisDecoderDecodeFull(&decoder_, p, avail, &out.zi, out.ops);
    if (!ZYAN_SUCCESS(st)) return false;
    out.length = out.zi.length;
    if (out.zi.mnemonic == ZYDIS_MNEMONIC_INT3) out.isEventCheck = true;
    return true;
}

void Disassembly::explore(uint16_t start) {
    // Immediate values loaded into registers, used to find trap handler addresses.
    std::map<ZydisRegister, uint16_t> imm;
    auto clearWritten = [&](const Insn& in) {
        if (in.isRt) {
            imm.erase(ZYDIS_REGISTER_BX);
            return;
        }
        for (int i = 0; i < in.zi.operand_count; ++i) {
            const ZydisDecodedOperand& op = in.ops[i];
            if (op.type == ZYDIS_OPERAND_TYPE_REGISTER && (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE))
                imm.erase(op.reg.value);
        }
    };

    uint16_t pc = start;
    for (;;) {
        if (insns_.count(pc)) return;
        Insn in;
        if (!decode(pc, in)) {
            std::fprintf(stderr, "warning: cannot decode instruction at %04x\n", pc);
            return;
        }
        insns_[pc] = in;

        if (in.isRt) {
            const RtCall& rt = in.rt;
            if (rt.intNo == 0x3f && (rt.op == 0x5d || rt.op == 0x5e)) {
                for (uint16_t t : rt.targets) {
                    labels_.insert(t);
                    if (rt.op == 0x5d) callTargets_.insert(t);
                    work_.push_back(t);
                }
            } else if (rt.is(0x3f, 0x58)) {
                labels_.insert(rt.word);
                callTargets_.insert(rt.word);
                work_.push_back(rt.word);
            } else if (rt.intNo == 0x3f && rt.op >= 0x02 && rt.op <= 0x08) {
                auto it = imm.find(ZYDIS_REGISTER_DX);
                if (it != imm.end() && it->second != 0) {
                    labels_.insert(it->second);
                    trapTargets_.insert(it->second);
                    work_.push_back(it->second);
                }
            } else if (rt.is(0x3f, 0x09)) {
                auto it = imm.find(ZYDIS_REGISTER_BX);
                if (it != imm.end()) {
                    labels_.insert(it->second);
                    trapTargets_.insert(it->second);
                    work_.push_back(it->second);
                }
            }
            if (opIsTerminal(rt.intNo, rt.op)) return;
            clearWritten(in);
            pc = in.next();
            continue;
        }

        if (in.zi.mnemonic == ZYDIS_MNEMONIC_MOV && in.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            in.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
            imm[in.ops[0].reg.value] = uint16_t(in.ops[1].imm.value.u);
        } else {
            clearWritten(in);
        }

        uint16_t target;
        if (in.isJmp()) {
            if (in.branchTarget(target)) {
                labels_.insert(target);
                work_.push_back(target);
            }
            return;
        }
        if (in.isJcc()) {
            if (in.branchTarget(target)) {
                labels_.insert(target);
                work_.push_back(target);
            }
        } else if (in.isCall()) {
            if (in.branchTarget(target)) {
                labels_.insert(target);
                callTargets_.insert(target);
                work_.push_back(target);
            } else if (in.farCallTarget(target)) {
                labels_.insert(target);
                farCallTargets_.insert(target);
                work_.push_back(target);
            }
        } else if (in.isRet() || in.isRetf() || in.zi.mnemonic == ZYDIS_MNEMONIC_IRET ||
                   in.zi.mnemonic == ZYDIS_MNEMONIC_HLT) {
            return;
        }
        pc = in.next();
    }
}

void Disassembly::run() {
    work_.push_back(prog_.codeStart);
    while (!work_.empty()) {
        uint16_t a = work_.back();
        work_.pop_back();
        explore(a);
    }
    // Statements after the last reachable one (e.g. after STOP): decode until
    // the zero padding that follows the code.
    for (int guard = 0; guard < 1000; ++guard) {
        uint16_t last = prog_.codeStart;
        bool sawEnd = false;
        for (const auto& kv : insns_) {
            last = std::max<uint16_t>(last, kv.second.next());
            if (kv.second.isRt && kv.second.rt.is(0x3e, 0x02)) sawEnd = true;
        }
        if (sawEnd) break; // the end of program opcode is the last compiled statement
        if (last + 1u >= prog_.codeEnd || (prog_.code[last] == 0 && prog_.code[last + 1] == 0)) break;
        Insn in;
        if (!decode(last, in)) break;
        explore(last);
        while (!work_.empty()) {
            uint16_t a = work_.back();
            work_.pop_back();
            explore(a);
        }
    }
    // Decode code that isn't reachable (statements after a GOTO with no label ...)
    // so it can still be shown. Only gaps between reachable code are considered.
    for (;;) {
        bool added = false;
        uint16_t prevEnd = prog_.codeStart;
        for (auto it = insns_.begin(); it != insns_.end(); ++it) {
            if (it->first > prevEnd) {
                bool allZero = true;
                for (uint16_t a = prevEnd; a < it->first; ++a)
                    if (prog_.code[a] != 0 && prog_.code[a] != 0x90) allZero = false;
                if (!allZero) {
                    work_.push_back(prevEnd);
                    added = true;
                    break;
                }
            }
            prevEnd = std::max<uint16_t>(prevEnd, it->second.next());
        }
        if (!added) break;
        while (!work_.empty()) {
            uint16_t a = work_.back();
            work_.pop_back();
            explore(a);
        }
    }
}

std::string Disassembly::format(const Insn& in) const {
    char buf[256];
    if (in.isRt) {
        const OpInfo& info = opInfo(in.rt.intNo, in.rt.op);
        std::string s;
        std::snprintf(buf, sizeof buf, "INT %02Xh/%02X %s", in.rt.intNo, in.rt.op, info.name);
        s = buf;
        for (uint8_t b : in.rt.bytes) {
            std::snprintf(buf, sizeof buf, " %02x", b);
            s += buf;
        }
        if (info.operand == InlineOperand::Word) {
            std::snprintf(buf, sizeof buf, " %04x", in.rt.word);
            s += buf;
        }
        for (uint16_t t : in.rt.targets) {
            std::snprintf(buf, sizeof buf, " ->%04x", t);
            s += buf;
        }
        return s;
    }
    if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&formatter_, &in.zi, in.ops, in.zi.operand_count_visible, buf,
                                                     sizeof buf, in.addr, ZYAN_NULL)))
        return buf;
    return "???";
}

void Disassembly::dump(FILE* out) const {
    for (const auto& kv : insns_) {
        const Insn& in = kv.second;
        if (labels_.count(in.addr)) std::fprintf(out, "loc_%04x:%s\n", in.addr, callTargets_.count(in.addr) ? "  ; called" : "");
        std::fprintf(out, "  %04x  ", in.addr);
        for (int i = 0; i < 8; ++i) {
            if (i < in.length) std::fprintf(out, "%02x", prog_.code[in.addr + i]);
            else std::fprintf(out, "  ");
        }
        std::fprintf(out, " %s\n", format(in).c_str());
    }
}

} // namespace qb
