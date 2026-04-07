/**
 * Convenience functions for creating a list (DT_ID) of letters (DT_LETTER), case-insensitive.
 * This is different from DT_NAME which has separate string storage.
 */

#ifndef STRING_H
#define STRING_H

#include "lang/TypedAtom.h"


Datum CreateString(char const * chars, size32 length);
Datum CreateStringFromCString(char const * cString);

bool IsString(Datum atom);

size32 GetStringLength(Datum string);

/**
 * Returne the letter (DT_LETTER) at the given position (1-based).
 */
Datum StringGetLetter(Datum string, index32 position);

void PrintString(Datum string);

Datum ParseString(char const * syntax, size32 length);


#endif //	STRING_H
