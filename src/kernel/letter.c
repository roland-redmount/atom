
#include <ctype.h>

#include "kernel/UInt.h"
#include "lang/Variable.h"
#include "kernel/letter.h"
#include "kernel/ifact.h"
#include "kernel/ServiceRegistry.h"


/**
 * TODO: Letters are fundamental atoms since they are required to create 
 * role names and named variables. We need to hardcode the
 * (letter code) relation since any table query would require a variable,
 * which in turn requires a (letter code) lookup.
 * 
 * In principle, letters should be a AT_ID identified by the letter code,
 * but for bootstrapping purposes we're now using a separate atom type AT_LETTER,
 * to avoid having to generate hash values for each letter. 
 * 
 * TODO: queries to the (letter code) relation must dispatch to these functions
 */

static uint8 charToLetterCode(char c)
{
	// we use 1,2, ... 26 for A, B, ..., Z
	uint8 letterCode = toupper(c) - 'A' + 1;
	ASSERT((letterCode >= 1) && (letterCode <= 26));
	return letterCode;	
}


static char letterCodeToChar(uint8 letterCode, uint8 letterCase)
{
	ASSERT((letterCode >= 1) && (letterCode <= 26));
	if(letterCase == LETTER_UPPERCASE)
		return 'A' + letterCode - 1;
	else
		return 'a' + letterCode - 1;

}


TypedAtom GetAlphabetLetter(char c)
{
	return CreateTypedAtom(AT_LETTER,charToLetterCode(c));
}


char LetterToChar(TypedAtom letter, uint8 letterCase)
{
	return letterCodeToChar(letter.atom, letterCase);
}


void PrintLetter(TypedAtom letter, uint8 letterCase)
{
	char c = LetterToChar(letter, letterCase);
	PrintChar('\'');
	PrintChar(c);
	PrintChar('\'');
}
