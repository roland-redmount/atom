/**
 * Miscellaneous "utility" functions that didn't fit anywhere else
 */

#ifndef UTILITIES_H
#define UTILITIES_H

#include "platform.h"

// useful macros

#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

/**
 * Integer divison ceiling.
 */
uint32 DivCeiling(uint32 numerator, uint32 divisor);

void SwapBytes(byte * a, byte * b);


/**
 * Printing arrays to stdout
 */
void PrintIndexArray(index32 const * array, size32 n);
void PrintIndex8Array(index8 const * array, size8 n);


#endif // UTILITIES_H
