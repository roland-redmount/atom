/**
 * Datum type for IEE754 floating point numbers
 * This implements 32-bit and 64-bit numbers.
 * We assume that the C float type is 32-bit, and double is 64-bit
 */

#include "lang/Atom.h"

#ifndef FLOATIEEE754_H
#define FLOATIEEE754_H


// create atoms
Atom CreateFloat32(float value);
Atom CreateFloat64(double value);

// get C values from atom
float GetFloat32Value(Atom float32);
double GetFloat64Value(Atom float64);

void PrintFloat32(Atom float32);
void PrintFloat64(Atom float64);


#endif //	FLOATIEEE754_H
