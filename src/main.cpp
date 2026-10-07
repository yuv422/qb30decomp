#include "decompiler.h"
#include "disasm.h"
#include "exe_loader.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: qb30decomp [options] program.exe\n"
                 "       qb30decomp [options] -d DIR program.exe program2.exe ...\n"
                 "\n"
                 "Decompiles a QuickBASIC 3.0 program (compiled for the BRUN30 runtime)\n"
                 "back to BASIC source.\n"
                 "\n"
                 "Programs that CHAIN to each other are best decompiled together: they\n"
                 "share their COMMON block, and what one program shows about it helps\n"
                 "with the others. Each NAME.EXE is written to DIR/NAME.BAS.\n"
                 "\n"
                 "options:\n"
                 "  -o FILE         write the BASIC source to FILE (default: stdout)\n"
                 "  -d DIR          decompile all the programs given into DIR\n"
                 "  --disasm        print an annotated disassembly instead of BASIC\n"
                 "  --dump-lifted   print the lifted basic blocks (debugging)\n"
                 "  --info          print information about the executable\n"
                 "  --no-comments   don't emit the header comment\n"
                 "  -v              verbose diagnostics (warnings to stderr)\n");
}

// QuickBASIC 3.0 needs CRLF line endings
bool writeSource(FILE* out, const std::string& source) {
    std::string text;
    for (char c : source) {
        if (c == '\n') text += "\r\n";
        else text += c;
    }
    return std::fwrite(text.data(), 1, text.size(), out) == text.size();
}

void reportWarnings(const std::string& name, const std::vector<std::string>& warnings, bool verbose) {
    if (verbose)
        for (const auto& w : warnings) std::fprintf(stderr, "%swarning: %s\n", name.c_str(), w.c_str());
    else if (!warnings.empty())
        std::fprintf(stderr, "%s%zu warning(s); use -v to show them\n", name.c_str(), warnings.size());
}

std::string baseName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    std::string b = s == std::string::npos ? path : path.substr(s + 1);
    size_t d = b.find_last_of('.');
    return d == std::string::npos ? b : b.substr(0, d);
}

// Decompiles programs that share a COMMON block. The layout each program shows
// is worked out twice: the second time with what the others showed the first time.
int decompileAll(const std::vector<std::string>& inputs, const std::string& dir, qb::DecompileOptions opt,
                 bool verbose) {
    std::vector<qb::LoadedProgram> progs;
    for (const std::string& in : inputs) progs.push_back(qb::loadProgram(in, verbose));
    std::vector<std::vector<qb::CommonItem>> layouts(progs.size());
    auto othersOf = [&](size_t i) {
        std::vector<qb::CommonItem> h;
        for (size_t j = 0; j < progs.size(); ++j)
            if (j != i) h.insert(h.end(), layouts[j].begin(), layouts[j].end());
        return h;
    };
    for (int round = 0; round < 2; ++round) {
        std::vector<std::vector<qb::CommonItem>> next(progs.size());
        for (size_t i = 0; i < progs.size(); ++i) next[i] = qb::commonLayout(progs[i], othersOf(i));
        layouts = next;
    }
    int status = 0;
    for (size_t i = 0; i < progs.size(); ++i) {
        opt.commonHints = othersOf(i);
        qb::DecompileResult r = qb::decompile(progs[i], opt);
        std::string path = dir + "/" + baseName(inputs[i]) + ".BAS";
        FILE* out = std::fopen(path.c_str(), "wb");
        if (!out || !writeSource(out, r.source)) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            status = 1;
        }
        if (out) std::fclose(out);
        reportWarnings(baseName(inputs[i]) + ": ", r.warnings, verbose);
    }
    return status;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> inputs;
    std::string output, dir;
    bool disasm = false, info = false, verbose = false;
    qb::DecompileOptions opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "-d" && i + 1 < argc) dir = argv[++i];
        else if (a == "--disasm") disasm = true;
        else if (a == "--dump-lifted") opt.dumpLifted = true;
        else if (a == "--info") info = true;
        else if (a == "--no-comments") opt.comments = false;
        else if (a == "-v") verbose = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { usage(); return 2; }
        else inputs.push_back(a);
    }
    if (inputs.empty() || (inputs.size() > 1 && dir.empty())) { usage(); return 2; }

    try {
        if (!dir.empty()) return decompileAll(inputs, dir, opt, verbose);
        qb::LoadedProgram prog = qb::loadProgram(inputs[0], verbose);
        FILE* out = stdout;
        if (!output.empty()) {
            out = std::fopen(output.c_str(), "wb");
            if (!out) { std::fprintf(stderr, "cannot write %s\n", output.c_str()); return 1; }
        }
        if (info) {
            std::fprintf(out, "format   %s\nmodule   %s\nsource   %s\nflags    %02x\ncode     %04x-%04x\nds end   %04x\n",
                         prog.format == qb::ExeFormat::BZ ? "bz" : "bm", prog.moduleName.c_str(), prog.sourceName.c_str(),
                         prog.flags, prog.codeStart, prog.codeEnd, prog.dsEnd);
            if (prog.commonSize) std::fprintf(out, "common   %04x-%04x\n", prog.commonStart, prog.commonEnd());
            for (const auto& d : prog.dataLines) std::fprintf(out, "data %04x: %s\n", d.key, d.text.c_str());
            for (int a = 0; a < 0x10000; ++a)
                if (prog.dsInit[a] && (a == 0 || !prog.dsInit[a - 1])) {
                    int e = a;
                    while (e < 0x10000 && prog.dsInit[e]) ++e;
                    std::fprintf(out, "init %04x-%04x\n", a, e);
                }
        } else if (disasm) {
            qb::Disassembly dis(prog);
            dis.run();
            dis.dump(out);
        } else {
            qb::DecompileResult r = qb::decompile(prog, opt);
            writeSource(out, r.source);
            reportWarnings("", r.warnings, verbose);
        }
        if (out != stdout) std::fclose(out);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
