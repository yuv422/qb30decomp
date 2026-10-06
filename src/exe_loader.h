// Loader for QuickBASIC 3.0 executables that use the BRUN30 runtime.
//
// Two layouts are supported:
//  - "bz": EXE files written directly by the QB.EXE environment. The load
//          module is a list of 16 byte aligned records (code, source name,
//          DATA statements, initialised data).
//  - "bm": EXE files produced by compiling to an OBJ and linking with
//          BRUN30.LIB. The load module starts with the code segment and the
//          initialised data follows it.
//
// In both cases the code segment begins with a header carrying the "bz"/"bm"
// signature and the compiled BASIC code starts at CS:0x30.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace qb {

enum class ExeFormat { BZ, BM };

struct DataLine {
    uint16_t key = 0;     // key used by RESTORE
    std::string text;     // raw text after the DATA keyword
};

struct LoadedProgram {
    ExeFormat format = ExeFormat::BZ;
    std::string moduleName;
    std::string sourceName;

    std::vector<uint8_t> code;    // code segment, indexed by CS offset
    uint16_t codeStart = 0x30;
    uint16_t codeEnd = 0;
    uint8_t flags = 0;            // compile flags byte at CS:0x2F (0x08 event trapping, 0x20 $DYNAMIC, 0x40 NOP padding)
    uint8_t flags2 = 0;           // byte at CS:0x2E (bit 0: compiled with /D)

    std::vector<uint8_t> ds;      // 64K image of the data segment
    std::vector<bool> dsInit;     // bytes with initial values (literals)
    uint16_t dsVarStart = 0x1856;
    uint16_t dsEnd = 0;

    std::vector<DataLine> dataLines;
    // ERL / TRON line table (/X, /E, /D): (code address, line number) pairs in
    // table order. Line 0 marks statements without a line number.
    std::vector<std::pair<uint16_t, uint16_t>> lineTable;

    bool debugBuild() const { return (flags2 & 0x01) != 0; }
    bool eventTrapping() const { return (flags & 0x08) != 0; }
    // Number of padding bytes that follow every runtime opcode byte.
    int opcodePadding() const { return (flags & 0x40) ? 2 : 0; }

    bool isInitialised(uint16_t addr, int len) const;
    uint16_t dsWord(uint16_t addr) const { return uint16_t(ds[addr] | (ds[uint16_t(addr + 1)] << 8)); }
    uint16_t codeWord(uint16_t addr) const { return uint16_t(code[addr] | (code[addr + 1] << 8)); }
};

// Throws std::runtime_error on failure.
LoadedProgram loadProgram(const std::string& path, bool verbose);

} // namespace qb
