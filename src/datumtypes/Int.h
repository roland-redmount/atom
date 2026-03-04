/**
 * Datum types for signed integers, two's complement, little endian
 */

#include "lang/Atom.h"

#ifndef INT_H
#define INT_H


Atom CreateInt(int64 value);
int64 GetIntValue(Atom a);

void PrintInt(Atom integer);


#endif		// INT_H
