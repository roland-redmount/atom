
#include "datumtypes/Int.h"
#include "datumtypes/UInt.h"		// for type conversion


TypedAtom CreateInt(int64 value)
{
	return (TypedAtom) {.type = DT_INT, .datum = value};
}

/**
 * Convert to C literal
 */
int64 GetIntValue(TypedAtom integer)
{
	return (int64) integer.datum;
}


void PrintInt(TypedAtom integer)
{
	PrintF("%lld", GetIntValue(integer));
}

