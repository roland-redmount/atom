/**
 * Datum types for signed integers, two's complement, little endian
 */

#include "lang/TypedAtom.h"

#ifndef INT_H
#define INT_H


TypedAtom CreateInt(int64 value);
int64 GetIntValue(TypedAtom a);

void PrintInt(TypedAtom integer);


#endif		// INT_H
