
#include "datumtypes/UInt.h"


/**
 * Create specific size atom
 */
TypedAtom CreateUInt(uint64 value)
{
	return (TypedAtom) {.type = DT_UINT, .datum = value};
}

/**
 * Convert to C literal
 */
uint64 GetUIntValue(TypedAtom integer)
{
	return (uint64) integer.datum;
}


void PrintUInt(TypedAtom integer)
{
	PrintF("%llu", GetUIntValue(integer));
}


