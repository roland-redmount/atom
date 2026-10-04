#include "lang/Variable.h"
#include "kernel/letter.h"
#include "kernel/ifact.h"


Atom CreateLetter(char c)
{
	ASSERT(IsLetterChar(c));
	if(IsUpperCaseLetterChar(c))
		 return (Atom) {.letter = {.code = c - 'A' + 1, .letterCase = LETTER_UPPERCASE}};
	else
		 return (Atom) {.letter = {.code = c - 'a' + 1, .letterCase = LETTER_LOWERCASE}};
}


char LetterToChar(Atom letter)
{
	ASSERT((letter.letter.code >= 1) && (letter.letter.code <= 26));
	if(letter.letter.letterCase == LETTER_UPPERCASE)
		return 'A' + letter.letter.code - 1;
	else
		return 'a' + letter.letter.code - 1;
}


void PrintLetter(Atom letter)
{
	char c = LetterToChar(letter);
	PrintChar('\'');
	PrintChar(c);
}
