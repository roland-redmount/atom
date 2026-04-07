/**
 * Datum type for insigned integers, little endian
 * 
 * NOTE: consider removing this so that we have only one integer datum.
 */

#include "lang/TypedAtom.h"

#ifndef UINT_H
#define UINT_H


TypedAtom CreateUInt(uint64 value);
uint64 GetUIntValue(TypedAtom integer);

void PrintUInt(TypedAtom integer);


#endif // UINT_H
