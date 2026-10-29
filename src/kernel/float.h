/**
 * Atom type for 64-bit IEE754 floating point numbers.
 */

#include "lang/TypedAtom.h"

#ifndef FLOATIEEE754_H
#define FLOATIEEE754_H


/* CLAUDE: Print a float with 6 decimals, as "%f" does. If those 6 decimals give the exact
 * value, trailing zeros are left out, keeping one decimal. Otherwise ".." follows the
 * decimals; see FormatFloat(). */
void PrintFloat(Atom _float);

/**
 * CLAUDE: Write a float to buffer as PrintFloat() prints it, and return the number of
 * characters written, not counting the terminating 0. For example, 1.5 is written "1.5",
 * 2.0 is written "2.0" and 1/64 is written "0.015625". The value 1/128 needs 7 decimals, so it
 * is written rounded, as "0.007812..". The value 0.1 has no exact binary representation, so
 * it is written "0.100000..". An infinite value or NaN is written as "%f" writes it.
 * The buffer must hold FLOAT_FORMAT_BUFFER_SIZE characters.
 */
size32 FormatFloat(double value, char buffer[], size32 bufferSize);

// CLAUDE: Enough for the largest double, which "%f" writes with 309 digits before the point
#define FLOAT_FORMAT_BUFFER_SIZE	330


#endif //	FLOATIEEE754_H
