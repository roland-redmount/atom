
#include "lang/Variable.h"
#include "kernel/ifact.h"
#include "kernel/letter.h"
#include "kernel/kernel.h"
#include "lang/name.h"
#include "lang/TermForm.h"
#include "library/list.h"
#include "library/string.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "storage/RelationBTree.h"


typedef struct s_StringLibrary {
	Atom stringRoleName;
	Atom stringPredicateForm;
	Atom stringTermForm;
	Relation stringRelation;
	TupleStore * stringTupleStore;
	Operator * stringOperator;
} StringLibrary;

//  pointer to the module state MODULE_STRING
static StringLibrary * stringLibrary = 0;


Atom GetStringRoleName(void)
{
	return stringLibrary->stringRoleName;
}


Atom GetStringPredicateForm(void)
{
	return stringLibrary->stringPredicateForm;
}


Atom GetStringTermForm(void)
{
	return stringLibrary->stringTermForm;
}


Relation GetStringRelation(void)
{
	return stringLibrary->stringRelation;
}


Atom CreateString(char const * chars, size32 length)
{
	IFactDraft draft;
	IFactBegin(&draft);

	// add the (list position element) ifacts
	Atom listElements[length];
	for(index32 i = 0; i < length; i++)
		listElements[i] = CreateLetter(chars[i]);
	AddListToIFact(&draft, listElements, AT_LETTER, length);

	// add (string @string) to ifact
	IFactBeginConjunction(&draft, stringLibrary->stringTupleStore, 0);
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
	// an AT_ID atom @a is a string if the (string @a) fact exists
	Atom arguments[1] = {atom};
	return OperatorCallOnce(stringLibrary->stringOperator, arguments);
}


void PrintString(Atom string)
{
	PrintChar('"');
	ListIterator iterator;
	ListIterate(string, &iterator);
	while(ListIteratorNext(&iterator)) {
		Atom letter = ListIteratorGetElement(&iterator);
		PrintChar(LetterToChar(letter));
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
	stringLibrary = Allocate(sizeof(StringLibrary));
	SetModuleState(MODULE_STRING, stringLibrary);

	// Create the (string) predicate form
	stringLibrary->stringRoleName = CreateNameFromCString("string");
	stringLibrary->stringPredicateForm = CreatePredicateForm((Atom[]) {stringLibrary->stringRoleName}, 1);
	NameRelease(stringLibrary->stringRoleName);
	
	// Create the (string) term form
	stringLibrary->stringTermForm = CreateTermForm(stringLibrary->stringPredicateForm, true);
	IFactRelease(stringLibrary->stringPredicateForm);

	// Create the (string:ID) relation, with B-tree provider
	TypeSignature typeSignature = {
		.atomTypes = {AT_ID}
	};
	stringLibrary->stringRelation = (Relation) {.form = stringLibrary->stringTermForm, .typeSignature = typeSignature};
	stringLibrary->stringTupleStore = CreateTupleStore(stringLibrary->stringRelation, GetStorageProvider(PROVIDER_BTREE), 1, 0);
	IFactRelease(stringLibrary->stringTermForm);

	// Store a pointer to the (string<ID) service, created by the B-tree provider.
	IOSignature ioSignature = {0};
	ioSignature.parameterIO[0] = PARAMETER_IN;
	stringLibrary->stringOperator = ServiceGetOperator((Service) {.relation = stringLibrary->stringRelation, .ioSignature = ioSignature});
	ASSERT(stringLibrary->stringOperator);
}


void StringShutdown(void)
{
	DropRelation(stringLibrary->stringRelation);

	Free(stringLibrary);
	SetModuleState(MODULE_STRING, 0);
	stringLibrary = 0;
}
