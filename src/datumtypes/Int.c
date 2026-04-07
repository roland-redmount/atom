
#include "datumtypes/Int.h"
#include "datumtypes/UInt.h"		// for type conversion


TypedAtom CreateInt(int64 value)
{
	return (TypedAtom) {.type = DT_INT, .atom = value};
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

