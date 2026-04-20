/**
 * Atom type for IEE754 floating point numbers
 * This implements 32-bit and 64-bit numbers.
 * We assume that the C float type is 32-bit, and double is 64-bit
 */

#include "lang/TypedAtom.h"

#ifndef FLOATIEEE754_H
#define FLOATIEEE754_H


// create atoms
TypedAtom CreateFloat32(float value);
TypedAtom CreateFloat64(double value);

// get C values from atom
float GetFloat32Value(TypedAtom float32);
double GetFloat64Value(TypedAtom float64);

void PrintFloat32(TypedAtom float32);
void PrintFloat64(TypedAtom float64);


#endif //	FLOATIEEE754_H
