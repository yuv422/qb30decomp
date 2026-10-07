#include "exe_loader.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace qb {

namespace {

uint16_t rd16(const std::vector<uint8_t>& b, size_t off) {
    if (off + 2 > b.size()) throw std::runtime_error("unexpected end of file");
    return uint16_t(b[off] | (b[off + 1] << 8));
}

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

// Parses a DATA statement block: (word key, zero terminated text)* ended by key 0xFFFF.
void parseDataBlock(const std::vector<uint8_t>& b, size_t off, size_t end, LoadedProgram& prog) {
    while (off + 2 <= end) {
        uint16_t key = rd16(b, off);
        off += 2;
        if (key == 0xFFFF) break;
        DataLine line;
        line.key = key;
        while (off < end && b[off] != 0) line.text += char(b[off++]);
        ++off;
        prog.dataLines.push_back(line);
    }
}

// Parses the line number table: (word address, word line)* ended by address 0.
void parseLineTable(const std::vector<uint8_t>& b, size_t off, size_t end, LoadedProgram& prog) {
    while (off + 4 <= end) {
        uint16_t addr = rd16(b, off);
        if (addr == 0) break;
        prog.lineTable.emplace_back(addr, rd16(b, off + 2));
        off += 4;
    }
}

void setDs(LoadedProgram& prog, uint16_t addr, uint8_t v) {
    prog.ds[addr] = v;
    prog.dsInit[addr] = true;
}

void loadBz(const std::vector<uint8_t>& file, size_t loadStart, LoadedProgram& prog, bool verbose) {
    size_t off = loadStart;
    bool haveCode = false;
    while (off + 16 <= file.size()) {
        uint16_t type = rd16(file, off);
        uint16_t length = rd16(file, off + 4);
        if (type < 0x10 || type > 0x1f) break;
        size_t data = off + 16;
        size_t end = data + length;
        if (end > file.size()) throw std::runtime_error("truncated record");
        if (verbose) std::fprintf(stderr, "record %02x length %04x at %06zx\n", type, length, off);
        switch (type) {
        case 0x15: // code segment
            prog.code.assign(file.begin() + long(data), file.begin() + long(end));
            haveCode = true;
            break;
        case 0x17: { // source file name
            std::string s;
            for (size_t i = data; i < end && file[i]; ++i) s += char(file[i]);
            prog.sourceName = s;
            break;
        }
        case 0x18: // DATA statements
            parseDataBlock(file, data, end, prog);
            break;
        case 0x1a: // line number table
            parseLineTable(file, data, end, prog);
            break;
        case 0x19: { // initialised data chunks: word size (incl. header), word address, bytes
            size_t p = data;
            while (p + 4 <= end) {
                uint16_t size = rd16(file, p);
                if (size < 4) break;
                uint16_t addr = rd16(file, p + 2);
                for (size_t i = 4; i < size && p + i < end; ++i)
                    setDs(prog, uint16_t(addr + i - 4), file[p + i]);
                p += size;
            }
            break;
        }
        default:
            break;
        }
        off = (end + 15) & ~size_t(15);
    }
    if (!haveCode) throw std::runtime_error("no code record found");
    prog.codeEnd = uint16_t(prog.code.size());
    prog.dsEnd = rd16(prog.code, 0x10);
}

void loadBm(const std::vector<uint8_t>& file, size_t loadStart, LoadedProgram& prog, bool verbose) {
    const std::vector<uint8_t>& f = file;
    uint16_t initPara = rd16(f, loadStart + 0x12);
    uint16_t initDs = rd16(f, loadStart + 0x20);
    uint16_t dataPtr = rd16(f, loadStart + 0x0c);
    size_t codeLen = size_t(initPara) * 16;
    if (loadStart + codeLen > f.size()) throw std::runtime_error("bad bm header");
    prog.code.assign(f.begin() + long(loadStart), f.begin() + long(loadStart + codeLen));
    prog.codeEnd = uint16_t(codeLen);
    // the BASIC code segment (paragraphs); the runtime start up code follows it
    uint16_t codePara = rd16(f, loadStart + 0x24);
    if (codePara && size_t(codePara) * 16 <= codeLen) prog.codeEnd = uint16_t(codePara * 16);
    prog.dsEnd = rd16(f, loadStart + 0x10);
    // the line number table follows the code (header word +0x0A, 0 if there is none)
    uint16_t lineTab = rd16(f, loadStart + 0x0a);
    if (lineTab >= 0x30 && lineTab < codeLen) parseLineTable(f, loadStart + lineTab, loadStart + codeLen, prog);

    // Initialised data is copied to DS:initDs. The DATA statement block follows the
    // literals; the header word at +0x0C points just past its first key.
    size_t initFile = loadStart + codeLen;
    uint16_t dataBlock = uint16_t(dataPtr - 2);
    if (verbose)
        std::fprintf(stderr, "bm: init data DS:%04x at file %06zx, DATA block DS:%04x\n", initDs, initFile, dataBlock);
    if (dataBlock >= initDs) {
        for (uint32_t a = initDs; a < dataBlock; ++a) {
            size_t fo = initFile + (a - initDs);
            if (fo >= f.size()) break;
            setDs(prog, uint16_t(a), f[fo]);
        }
        parseDataBlock(f, initFile + (dataBlock - initDs), f.size(), prog);
    }

    // The code starts with far calls into the runtime start up code. Skip them.
    uint16_t pc = 0x30;
    while (pc + 5 <= prog.codeEnd && prog.code[pc] == 0x9a) pc += 5;
    prog.codeStart = pc;
}

} // namespace

bool LoadedProgram::isInitialised(uint16_t addr, int len) const {
    for (int i = 0; i < len; ++i)
        if (!dsInit[uint16_t(addr + i)]) return false;
    return true;
}

LoadedProgram loadProgram(const std::string& path, bool verbose) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file.size() < 0x40 || file[0] != 'M' || file[1] != 'Z') throw std::runtime_error("not an MZ executable");

    size_t loadStart = size_t(rd16(file, 8)) * 16;
    LoadedProgram prog;
    prog.ds.assign(0x10000, 0);
    prog.dsInit.assign(0x10000, false);

    if (loadStart + 16 > file.size()) throw std::runtime_error("bad MZ header");
    if (file[loadStart] == 'b' && file[loadStart + 1] == 'm') {
        prog.format = ExeFormat::BM;
        loadBm(file, loadStart, prog, verbose);
    } else if (rd16(file, loadStart) == 0x15 && file[loadStart + 16] == 'b' && file[loadStart + 17] == 'z') {
        prog.format = ExeFormat::BZ;
        loadBz(file, loadStart, prog, verbose);
    } else {
        throw std::runtime_error("not a QuickBASIC 3.0 BRUN30 executable (no bz/bm signature)");
    }

    if (prog.code.size() < 0x30) throw std::runtime_error("code segment too small");
    prog.moduleName = trimRight(std::string(prog.code.begin() + 2, prog.code.begin() + 10));
    prog.flags = prog.code[0x2f];
    prog.commonStart = prog.codeWord(0x18);
    prog.commonSize = prog.codeWord(0x1a);
    // variables start right at the COMMON block when there is one
    if (prog.commonSize) prog.dsVarStart = prog.commonStart;
    prog.flags2 = prog.code[0x2e];
    if (verbose)
        std::fprintf(stderr, "module %s format %s flags %02x code %04x-%04x ds end %04x\n", prog.moduleName.c_str(),
                     prog.format == ExeFormat::BZ ? "bz" : "bm", prog.flags, prog.codeStart, prog.codeEnd, prog.dsEnd);
    return prog;
}

} // namespace qb
