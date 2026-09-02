
#include "lang/Variable.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "lang/name.h"
#include "lang/TermForm.h"
#include "library/list.h"
#include "library/string.h"
#include "storage/RelationBTree.h"


static Atom stringRoleName;
static Atom stringPredicateForm;
static Atom stringTermForm;
static Relation const * stringRelation;
static RelationTable * stringRelationTable;
static Operator * stringOperator;


Atom GetStringRoleName(void)
{
	return stringRoleName;
}


Atom GetStringPredicateForm(void)
{
	return stringPredicateForm;
}


Atom GetStringTermForm(void)
{
	return stringTermForm;
}


Relation const * GetStringRelation(void)
{
	return stringRelation;
}


RelationTable * GetStringRelationTable(void)
{
	return stringRelationTable;
}


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
	IFactBeginConjunction(&draft, stringRelationTable,0);
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
	return AtomHasRole(atom, stringRelation, stringRoleName);
}


void PrintString(Atom string)
{
	PrintChar('"');
	ListIterator iterator;
	ListIterate(string, &iterator);
	while(ListIteratorNext(&iterator)) {
		Atom letter = ListIteratorGetElement(&iterator);
		PrintChar(LetterToChar(letter, LETTER_UPPERCASE));
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


void StringSetup(void)
{
	// Create the (string) predicate form
	stringRoleName = CreateNameFromCString("string");
	stringPredicateForm = CreatePredicateForm((Atom[]) {stringRoleName},	1);
	NameRelease(stringRoleName);
	
	// Create the (string) term form
	stringTermForm = CreateTermForm(stringPredicateForm, true);
	IFactRelease(stringPredicateForm);

	// Create the (string:ID) relation, with B-tree provider
	TypeSignature typeSignature = {
		.atomTypes = {AT_ID}
	};
	stringRelation = CreateRelation(stringTermForm, 1, typeSignature);
	IFactRelease(stringTermForm);

	stringRelationTable = CreateRelationTable(stringRelation, &btreeTableProvider, 0);
	ReleaseRelation(stringRelation);

	// Store a pointer to the (string<ID) service, created by the B-tree provider.
	IOSignature ioSignature = {0};
	ioSignature.parameterIO[0] = PARAMETER_IN;
	stringOperator = ServiceRegistryFind(stringRelation, ioSignature);
	ASSERT(stringOperator);
}


void StringShutdown(void)
{
	ReleaseRelationTable(stringRelationTable);
}
