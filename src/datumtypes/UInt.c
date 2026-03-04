
#include "datumtypes/UInt.h"


/**
 * Create specific size atom
 */
Atom CreateUInt(uint64 value)
{
	return (Atom) {DT_UINT, 0, 0, 0, value};
}

/**
 * Convert to C literal
 */
uint64 GetUIntValue(Atom integer)
{
	return (uint64) integer.datum;
}


void PrintUInt(Atom integer)
{
	PrintF("%llu", GetUIntValue(integer));
}


