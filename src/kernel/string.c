
#include "lang/Variable.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/RelationBTree.h"
#include "kernel/string.h"


Atom stringElementGenerator(index32 index, void const * data)
{
	char const * string = (char const *) data;
	return GetAlphabetLetter(string[index]);
}


Atom CreateString(char const * chars, size32 length)
{
	IFactDraft draft;
	IFactBegin(&draft);

	AddListToIFact(&draft, stringElementGenerator, chars, AT_LETTER, length);

	// add (string @string) to ifact
	IFactBeginConjunction(
		&draft,
		GetCoreRelationTable(RELATION_STRING),
		0
	);
	Atom tuple[1] = {(Atom) {0}};
	IFactAddTuple(&draft, tuple);
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


Atom CreateStringFromCString(char const * cString)
{
	return CreateString(cString, CStringLength(cString));
}


bool IsString(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCoreRelationTable(RELATION_STRING),
		GetCoreRoleName(ROLE_STRING)
	);
}


// TODO: printing case should be configurable, lower/upper/sentence/camel case
void PrintString(Atom string)
{
	PrintChar('"');
	ListIterator iterator;
	ListIterate(string, &iterator);
	while(ListIteratorNext(&iterator)) {
		Atom letter = ListIteratorGetElement(&iterator);
		PrintChar(LetterToChar(letter, LETTER_LOWERCASE));
	}
	ListIteratorEnd(&iterator);
	PrintChar('"');
}


/**
 * Parse a string literal starting at the given string pointer
 * (no whitespace allowed)
 */
Atom ParseString(char const * syntax, size32 length)
{
	// check syntax
	ASSERT(syntax[0] == '\"');
	ASSERT(syntax[length-1] == '\"');
	// create atom, skipping " "
	return CreateString(syntax + 1, length - 2);
}

