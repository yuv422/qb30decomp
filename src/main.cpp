#include "decompiler.h"
#include "disasm.h"
#include "exe_loader.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: qb30decomp [options] program.exe\n"
                 "\n"
                 "Decompiles a QuickBASIC 3.0 program (compiled for the BRUN30 runtime)\n"
                 "back to BASIC source.\n"
                 "\n"
                 "options:\n"
                 "  -o FILE         write the BASIC source to FILE (default: stdout)\n"
                 "  --disasm        print an annotated disassembly instead of BASIC\n"
                 "  --dump-lifted   print the lifted basic blocks (debugging)\n"
                 "  --info          print information about the executable\n"
                 "  --no-comments   don't emit the header comment\n"
                 "  -v              verbose diagnostics (warnings to stderr)\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string input, output;
    bool disasm = false, info = false, verbose = false;
    qb::DecompileOptions opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];
        else if (a == "--disasm") disasm = true;
        else if (a == "--dump-lifted") opt.dumpLifted = true;
        else if (a == "--info") info = true;
        else if (a == "--no-comments") opt.comments = false;
        else if (a == "-v") verbose = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') { usage(); return 2; }
        else input = a;
    }
    if (input.empty()) { usage(); return 2; }

    try {
        qb::LoadedProgram prog = qb::loadProgram(input, verbose);
        FILE* out = stdout;
        if (!output.empty()) {
            out = std::fopen(output.c_str(), "wb");
            if (!out) { std::fprintf(stderr, "cannot write %s\n", output.c_str()); return 1; }
        }
        if (info) {
            std::fprintf(out, "format   %s\nmodule   %s\nsource   %s\nflags    %02x\ncode     %04x-%04x\nds end   %04x\n",
                         prog.format == qb::ExeFormat::BZ ? "bz" : "bm", prog.moduleName.c_str(), prog.sourceName.c_str(),
                         prog.flags, prog.codeStart, prog.codeEnd, prog.dsEnd);
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
            // QuickBASIC 3.0 needs CRLF line endings
            std::string text;
            for (char c : r.source) {
                if (c == '\n') text += "\r\n";
                else text += c;
            }
            std::fwrite(text.data(), 1, text.size(), out);
            if (verbose)
                for (const auto& w : r.warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
            else if (!r.warnings.empty())
                std::fprintf(stderr, "%zu warning(s); use -v to show them\n", r.warnings.size());
        }
        if (out != stdout) std::fclose(out);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
