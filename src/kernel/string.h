/**
 * Convenience functions for creating a list of letters, case-insensitive.
 * This is different from DT_NAME which has separate string storage.
 */

#ifndef STRING_H
#define STRING_H

#include "lang/Atom.h"


Atom CreateString(char const * chars, size32 length);
Atom CreateStringFromCString(char const * cString);

bool IsString(Atom atom);

size32 GetStringLength(Atom string);
Atom StringGetLetter(Atom string, index32 position);

void PrintString(Atom string);

Atom ParseString(char const * syntax, size32 length);


#endif //	STRING_H
