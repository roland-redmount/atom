
#include "lang/DatumType.h"


// this array specifies a printable name string for each of the N_DATUMTYPES
char const * datumTypeNames[] = {
	"INVALID",
	"UNKNOWN",
	"DATUMTYPE",
	"UINT",
	"INT",
	"FLOAT32",
	"FLOAT64",
	"LETTER",
	"VARIABLE",
	"NAME",
	"ID",
	"INSTRUCTION",
	"PARAMETER"
};


char const * GetDatumTypeName(byte typeId)
{
	return datumTypeNames[typeId];
}


// compare a fixed-size string to a zero-terminated (C) string
static bool equalStringToCString(char const * string, size32 length, char const * cstring)
{
	return (CStringCompareLimited(string, cstring, length) == 0) && (cstring[length] == 0);
}



byte DatumTypeIdFromString(char const * string, size32 length)
{
//	printf("DatumTypeIdFromString() string = %.*s length = %llu\n", (int) length, string, length);
	ASSERT(length != 0);
	// check known type strings
	for(index8 i = 0; i < N_DATUMTYPES; i++) {
		if(equalStringToCString(string, length, datumTypeNames[i]))
			return i;
	}
	return 0;
}

