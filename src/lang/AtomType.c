
#include "lang/AtomType.h"


// this array specifies a printable name string for each of the N_DATUMTYPES
char const * atomTypeNames[N_ATOMTYPES + 1] = {
	"NONE",
	"NAME",
	"ID",
	"UINT",
	"INT",
	"FLOAT",
	"LETTER",
	"VARIABLE",
	"PARAMETER",
};


char const * GetAtomTypeName(byte type)
{
	return atomTypeNames[type];
}


// compare a fixed-size string to a zero-terminated (C) string
static bool equalStringToCString(char const * string, size32 length, char const * cstring)
{
	return (CStringCompareLimited(string, cstring, length) == 0) && (cstring[length] == 0);
}


byte AtomTypeFromString(char const * string, size32 length)
{
	ASSERT(length != 0);
	// check known type strings. AT_NONE is name 0 and the atom types run to N_ATOMTYPES,
	// so the last name is at that index rather than one below it.
	for(index8 i = 0; i <= N_ATOMTYPES; i++) {
		if(equalStringToCString(string, length, atomTypeNames[i]))
			return i;
	}
	return 0;
}

