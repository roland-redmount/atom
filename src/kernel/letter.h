/**
 * A letter of the English alphabet, case-insensitive.
 */


#ifndef LETTER_H
#define LETTER_H

#include "lang/TypedAtom.h"


#define LETTER_LOWERCASE	0
#define LETTER_UPPERCASE	1


TypedAtom GetAlphabetLetter(char c);

char LetterToChar(TypedAtom letter, uint8 letterCase);

void PrintLetter(TypedAtom letter, uint8 letterCase);

#endif  // LETTER_H
