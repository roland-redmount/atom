
#include "datumtypes/Variable.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/list.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/RelationBTree.h"
#include "kernel/string.h"
#include "kernel/ServiceRegistry.h"


Atom stringElementGenerator(index32 index, void const * data)
{
	char const * string = (char const *) data;
	return GetAlphabetLetter(string[index]);
}

Datum CreateString(char const * chars, size32 length)
{
	IFactDraft draft;
	IFactBegin(&draft);

	AddListToIFact(&draft, stringElementGenerator, chars, length);

	// add (string @string) to ifact
	IFactBeginConjunction(
		&draft,
		GetCorePredicateForm(FORM_STRING),
		0
	);
	Atom string = invalidAtom;
	IFactAddClause(&draft, &string);
	IFactEndConjunction(&draft);

	return IFactEnd(&draft);
}


Datum CreateStringFromCString(char const * cString)
{
	return CreateString(cString, CStringLength(cString));
}


bool IsString(Datum atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_STRING),
		GetCoreRoleName(ROLE_STRING)
	);
}

Datum StringGetLetter(Datum string, index32 position)
{
	Atom element = ListGetElement(string, position);
	ASSERT(element.type == DT_LETTER)
	return element.datum;
}


size32 GetStringLength(Datum string)
{
	return ListLength(string);
}


// TODO: printing case should be configurable, lower/upper/sentence/camel case
void PrintString(Datum string)
{
	PrintChar('"');
	ListIterator iterator;
	ListIterate(string, &iterator);
	while(ListIteratorHasNext(&iterator)) {
		Atom letter = ListIteratorGetElement(&iterator);
		PrintChar(LetterToChar(letter, LETTER_LOWERCASE));
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
	PrintChar('"');
}


/**
 * Parse a string literal starting at the given string pointer
 * (no whitespace allowed)
 */
Datum ParseString(char const * syntax, size32 length)
{
	// check syntax
	ASSERT(syntax[0] == '\"');
	ASSERT(syntax[length-1] == '\"');
	// create atom, skipping " "
	return CreateString(syntax + 1, length - 2);
}

