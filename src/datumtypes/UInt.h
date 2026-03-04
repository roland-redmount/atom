/**
 * Datum type for insigned integers, little endian
 * 
 * NOTE: consider removing this so that we have only one integer datum.
 */

#include "lang/Atom.h"

#ifndef UINT_H
#define UINT_H


Atom CreateUInt(uint64 value);
uint64 GetUIntValue(Atom integer);

void PrintUInt(Atom integer);


#endif // UINT_H
