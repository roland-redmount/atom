
#include "datumtypes/UInt.h"


/**
 * Create specific size atom
 */
Atom CreateUInt(uint64 value)
{
	return (Atom) {.type = DT_UINT, .datum = value};
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


