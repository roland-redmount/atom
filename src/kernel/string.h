/**
 * Convenience functions for creating a list (AT_ID) of letters (AT_LETTER), case-insensitive.
 * This is different from AT_NAME which has separate string storage.
 */

#ifndef STRING_H
#define STRING_H

#include "lang/TypedAtom.h"


Atom CreateString(char const * chars, size32 length);
Atom CreateStringFromCString(char const * cString);

bool IsString(Atom atom);

size32 GetStringLength(Atom string);

/**
 * Returne the letter (AT_LETTER) at the given position (1-based).
 */
Atom StringGetLetter(Atom string, index32 position);

void PrintString(Atom string);

Atom ParseString(char const * syntax, size32 length);


#endif //	STRING_H
