#include "mbf.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace qb {

double mbfSingleToDouble(const uint8_t* p) {
    int exp = p[3];
    if (exp == 0) return 0.0;
    bool neg = (p[2] & 0x80) != 0;
    uint32_t mant = 0x800000u | (uint32_t(p[2] & 0x7f) << 16) | (uint32_t(p[1]) << 8) | p[0];
    double v = std::ldexp(double(mant), exp - 128 - 24);
    return neg ? -v : v;
}

double mbfDoubleToDouble(const uint8_t* p) {
    int exp = p[7];
    if (exp == 0) return 0.0;
    bool neg = (p[6] & 0x80) != 0;
    uint64_t mant = 0x80ull | (p[6] & 0x7f);
    for (int i = 5; i >= 0; --i) mant = (mant << 8) | p[i];
    // 56 bit mantissa. Converting to a double rounds off the lowest 3 bits.
    double v = std::ldexp(double(mant), exp - 128 - 56);
    return neg ? -v : v;
}

namespace {

// Shortest decimal text for v that reads back to the same value with the
// given round trip test. Uses plain notation for "normal" magnitudes.
template <class Same>
std::string shortest(double v, int maxDigits, Same same, char expChar) {
    char buf[64];
    double a = std::fabs(v);
    if (v == 0) return "0";
    // integers
    if (a < 1e15 && std::floor(a) == a) {
        std::snprintf(buf, sizeof buf, "%.0f", v);
        if (same(std::strtod(buf, nullptr)) && std::strlen(buf) <= size_t(maxDigits + 1)) return buf;
    }
    bool plain = a >= 1e-4 && a < 1e7;
    if (plain) {
        for (int dec = 1; dec <= maxDigits + 4; ++dec) {
            std::snprintf(buf, sizeof buf, "%.*f", dec, v);
            if (same(std::strtod(buf, nullptr))) {
                // strip trailing zeros
                std::string s = buf;
                while (!s.empty() && s.back() == '0') s.pop_back();
                if (!s.empty() && s.back() == '.') s.pop_back();
                // count significant digits; too many means the value needs more precision
                int digits = 0;
                bool lead = true;
                for (char c : s) {
                    if (c >= '0' && c <= '9') {
                        if (c != '0') lead = false;
                        if (!lead) ++digits;
                    }
                }
                if (digits <= maxDigits) return s;
                break;
            }
        }
    }
    for (int prec = 1; prec <= maxDigits; ++prec) {
        std::snprintf(buf, sizeof buf, "%.*e", prec - 1, v);
        if (same(std::strtod(buf, nullptr))) break;
    }
    // mantissa e+XX -> mantissaE+XX
    std::string s = buf;
    size_t e = s.find('e');
    std::string mant = s.substr(0, e);
    if (mant.find('.') != std::string::npos) {
        while (mant.back() == '0') mant.pop_back();
        if (mant.back() == '.') mant.pop_back();
    }
    int ex = std::atoi(s.c_str() + e + 1);
    return mant + expChar + (ex < 0 ? "-" : "+") + std::to_string(std::abs(ex));
}

} // namespace

std::string formatSingle(double v) {
    float target = float(v);
    std::string s = shortest(double(target), 9, [&](double x) { return float(x) == target; }, 'E');
    // A literal with more than 7 digits would be read as double precision.
    size_t digits = 0;
    for (char c : s) {
        if (c == 'E' || c == 'D') break;
        if (c >= '0' && c <= '9') ++digits;
    }
    if (digits > 7) s += "!";
    return s;
}

std::string formatDouble(double v) {
    std::string s = shortest(v, 17, [&](double x) { return x == v; }, 'D');
    if (s.find('D') == std::string::npos) s += "#";
    return s;
}

} // namespace qb
