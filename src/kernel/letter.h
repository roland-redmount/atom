/**
 * A letter of the English alphabet.
 * 
 * NOTE: this might not need a dedicated atom type. We could define
 * a letter as (letter * code x) where x is an integer, and a case-sensitive
 * letter as (caseletter * letter l case c). However, for this we need a
 * mature print/visualize method; currently, PrintTypedAtom() only differentiates
 * per atom type.
 */


#ifndef LETTER_H
#define LETTER_H

#include "lang/Atom.h"


#define LETTER_LOWERCASE	0
#define LETTER_UPPERCASE	1

/**
 * Create a letter atom from a C char. The char must satisfy IsAlpha()
 */
Atom CreateLetter(char c);

char LetterToChar(Atom letter);

void PrintLetter(Atom letter);

#endif  // LETTER_H
