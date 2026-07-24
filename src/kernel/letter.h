/**
 * A letter of the English alphabet, case-insensitive.
 */


#ifndef LETTER_H
#define LETTER_H

#include "lang/Atom.h"


#define LETTER_LOWERCASE	0
#define LETTER_UPPERCASE	1


Atom GetAlphabetLetter(char c);

char LetterToChar(Atom letter, uint8 letterCase);

void PrintLetter(Atom letter, uint8 letterCase);

#endif  // LETTER_H
