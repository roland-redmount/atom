
#include "kernel/UInt.h"


/**
 * Create specific size atom
 */
TypedAtom CreateUInt(uint64 value)
{
	return (TypedAtom) {.type = AT_UINT, .atom = value};
}

/**
 * Convert to C literal
 */
uint64 GetUIntValue(TypedAtom integer)
{
	return (uint64) integer.atom;
}


void PrintUInt(TypedAtom integer)
{
	PrintF("%llu", GetUIntValue(integer));
}


