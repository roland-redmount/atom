
#include "datumtypes/Int.h"
#include "datumtypes/UInt.h"		// for type conversion


Atom CreateInt(int64 value)
{
	return (Atom) {DT_INT, 0, 0, 0, value};
}

/**
 * Convert to C literal
 */
int64 GetIntValue(Atom integer)
{
	return (int64) integer.datum;
}


void PrintInt(Atom integer)
{
	PrintF("%lld", GetIntValue(integer));
}

