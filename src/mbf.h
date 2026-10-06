// Microsoft Binary Format (MBF) floating point helpers.
//
// QuickBASIC 3.0 (non-coprocessor version) stores single and double precision
// numbers in MBF. A single is 4 bytes: 3 mantissa bytes (sign in bit 7 of the
// third byte) followed by the exponent. A double has 7 mantissa bytes.
#pragma once

#include <cstdint>
#include <string>

namespace qb {

double mbfSingleToDouble(const uint8_t* p);
double mbfDoubleToDouble(const uint8_t* p);

// Formats a single precision value as a BASIC literal that will be parsed back
// to the same single precision value.
std::string formatSingle(double v);

// Formats a double precision value as a BASIC literal. Appends '#' when the
// literal would otherwise be read as single precision.
std::string formatDouble(double v);

} // namespace qb
