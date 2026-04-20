
#include "kernel/Int.h"


TypedAtom CreateInt(int64 value)
{
	return (TypedAtom) {.type = AT_INT, .atom = value};
}

/**
 * Convert to C literal
 */
int64 GetIntValue(TypedAtom integer)
{
	return (int64) integer.atom;
}


void PrintInt(TypedAtom integer)
{
	PrintF("%lld", GetIntValue(integer));
}

