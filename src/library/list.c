
#include "lang/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/Relation.h"
#include "kernel/ServiceRegistry.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/list.h"
#include "memory/allocator.h"
#include "memory/paging.h"
#include "storage/RelationBTree.h"
#include "util/hashing.h"


typedef struct s_ListLibrary {
	Atom listRoleName;
	Atom positionRoleName;
	Atom elementRoleName;
	Atom lengthRoleName;

	Atom listPredicateForm;
	Atom listTermForm;
	index8 listRoleIndex[3];

	Atom listLengthPredicateForm;
	Atom listLengthTermForm;
	index8 listLengthRoleIndex[2];

	Relation listIDRelation;
	TupleStore * listIDTupleStore;
	Operator * listIDOperator;

	Relation listLetterRelation;
	TupleStore * listLetterTupleStore;
	Operator * listLetterOperator;

	Relation listLengthRelation;
	TupleStore * listLengthTupleStore;
	Operator * listLengthOperator;
} ListLibrary;


static ListLibrary * listLibrary = 0;


Atom GetListPredicateForm(void)
{
	return listLibrary->listPredicateForm;
}


Atom GetListLengthTermForm(void)
{
	return listLibrary->listLengthTermForm;
}


index8 const * GetListRoleIndex(void)
{
	return listLibrary->listRoleIndex;
}


void ListSetTuple(Atom const inputTuple[], Atom tuple[])
{
	TupleCopyPermuted(inputTuple, tuple, listLibrary->listRoleIndex, 3);
}


void ListSetByteArray(byte const inputArray[], byte array[])
{
	CopyBytesPermuted(inputArray, array, listLibrary->listRoleIndex, 3);
}


index8 const * GetListLengthRoleIndex(void)
{
	return listLibrary->listLengthRoleIndex;
}


Relation GetListRelation(byte elementType)
{
	switch(elementType) {
	case AT_ID:
		return listLibrary->listIDRelation;

	case AT_LETTER:
		return listLibrary->listLetterRelation;

	default:
		ASSERT(false)
		return (Relation) {0};
	}
}


Operator * GetListOperator(byte elementType)
{
	switch(elementType) {
	case AT_ID:
		return listLibrary->listIDOperator;

	case AT_LETTER:
		return listLibrary->listLetterOperator;

	default:
		ASSERT(false)
		return 0;
	}
}


Relation GetListLengthRelation(void)
{
	return listLibrary->listLengthRelation;
}


Operator * GetListLengthOperator(void)
{
	return listLibrary->listLengthOperator;
}


Atom CreateListFromArray(Atom const elements[], byte elementType, size8 nElements)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddListToIFact(&draft, elements, elementType, nElements);
	return IFactEnd(&draft);
}

/**
 * Create the (list length) ifact
 */
static void assertListLength(IFactDraft * draft, size32 nElements)
{
	IFactBeginConjunction(draft, listLibrary->listLengthTupleStore, listLibrary->listLengthRoleIndex[0]);

	Atom listLengthTuple[2];
	listLengthTuple[listLibrary->listLengthRoleIndex[1]] = (Atom) {._int = nElements};
	IFactAddTuple(draft, listLengthTuple);
	IFactEndConjunction(draft);	
}


/**
 * Add the identifying facts for a list to the given draft ifact.
 */
void AddListToIFact(IFactDraft * draft, Atom const elements[], byte elementType, size32 nElements)
{
	if(nElements > 0) {
		Relation relation = GetListRelation(elementType);
		// assert (ĺist position elements) facts for each element
		IFactBeginConjunction(draft, RelationGetTupleStore(relation), listLibrary->listRoleIndex[0]);
		Atom listElementTuple[3];
		for(index32 i = 0; i < nElements; i++) {
			TupleCopyPermuted(
				(Atom[]) {(Atom) {0}, (Atom) {._int = i + 1}, elements[i]},
				listElementTuple, listLibrary->listRoleIndex, 3
			);
			IFactAddTuple(draft, listElementTuple);
		}
		IFactEndConjunction(draft);
	}
	assertListLength(draft, nElements);
}


bool IsList(Atom atom)
{
	// We define this from the (list length) relation since
	// there may be no (list element position) fact if atom is an empty list.
	Atom arguments[2];
	arguments[listLibrary->listLengthRoleIndex[0]] = atom;
	return OperatorCallOnce(listLibrary->listLengthOperator, arguments);
}


size32 ListLength(Atom list)
{
	Atom arguments[2];
	arguments[listLibrary->listLengthRoleIndex[0]] = list;
	ASSERT(OperatorCallOnce(listLibrary->listLengthOperator, arguments))
	return (size32) arguments[listLibrary->listLengthRoleIndex[1]]._int;
}


/**
 * Determine the (list position element) relation that the given list
 * participates in, depending on its element type.
 * 
 * TODO: this is not well-defined in general, there may be > 1 relation for lists
 * containing mixed types, although CreateList() does not yields such lists.
 */
static Relation findListElementRelation(Atom list)
{
	Atom arguments[3];
	arguments[listLibrary->listRoleIndex[0]] = list;
	
	if(OperatorCallOnce(listLibrary->listIDOperator, arguments))
		return listLibrary->listIDRelation;
	
	if(OperatorCallOnce(listLibrary->listLetterOperator, arguments))
		return listLibrary->listLetterRelation;
	
	return (Relation) {0};
}


Atom ListGetElement(Atom list, index32 position)
{
	ASSERT(ListLength(list) > 0)
	Relation relation = findListElementRelation(list);
	ASSERT(!IsNullRelation(relation))

	byte parameterIO[3];
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_IN, PARAMETER_OUT}, parameterIO, listLibrary->listRoleIndex, 3);
	Operator const * op = ServiceGetOperator(
		(Service) {.relation = relation, .ioSignature = CreateIOSignature(parameterIO, 3)}
	);
	Atom arguments[3];
	arguments[listLibrary->listRoleIndex[0]] = list;
	arguments[listLibrary->listRoleIndex[1]] = (Atom) {._int = position};
	ASSERT(OperatorCallOnce(op, arguments))
	return arguments[listLibrary->listRoleIndex[2]];
}


index32 ListGetPosition(Atom list, Atom element)
{
	ASSERT(IsList(list))
	Relation relation = findListElementRelation(list);
	ASSERT(!IsNullRelation(relation))

	// TODO: this service is not one the B-tree provider registers, as its inputs are not
	// a prefix of the index column order; see RelationTableProvider.registerServices().
	// Calling this function will trigger the ASSERT below, unless a query has already
	// compiled a FILTER service for the pattern; see seedVariantsFromServices() in compiler.c.
	// Asking the compiler here, or an array-based storage provider, would give one.

	byte parameterIO[3];
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_IN}, parameterIO, listLibrary->listRoleIndex, 3);
	Operator const * op = ServiceGetOperator(
		(Service) {.relation = relation, .ioSignature = CreateIOSignature(parameterIO, 3)}
	);

	Atom arguments[3];
	arguments[listLibrary->listRoleIndex[0]] = list;
	arguments[listLibrary->listRoleIndex[2]] = element;
	ASSERT(OperatorCallOnce(op, arguments))
	return arguments[listLibrary->listRoleIndex[1]]._int;
}


int8 ListLexicalOrdering(Atom list1, Atom list2, int8 (*compare)(Atom, Atom))
{
	if(SameAtoms(list1, list2))
		return 0;

	ListIterator iterator1;
	ListIterate(list1, &iterator1);
	ListIterator iterator2;
	ListIterate(list2, &iterator2);

	int8 listOrder = 0;
	while(true) {
		bool hasNext1 = ListIteratorNext(&iterator1);
		bool hasNext2 = ListIteratorNext(&iterator2);
		if(!hasNext1 && hasNext2) {
			listOrder = -1;  // list1 is a prefix of list2
			break;
		}
		if(hasNext1 && !hasNext2) {
			listOrder = 1;  // list2 is a prefix of list1
			break;
		}
		ASSERT(hasNext1 && hasNext2);
		Atom atom1 = ListIteratorGetElement(&iterator1);
		Atom atom2 = ListIteratorGetElement(&iterator2);
		int8 atomOrder = compare(atom1, atom2);
		if(atomOrder != 0) {
			listOrder = atomOrder;
			break;
		}
	}
	ListIteratorEnd(&iterator1);
	ListIteratorEnd(&iterator2);
	ASSERT(listOrder != 0);	// distinct, unique strings cannot be equal
	return listOrder;
}


void ListIterate(Atom list, ListIterator * iterator)
{
	iterator->queryTuple[listLibrary->listRoleIndex[0]] = list;

	if(ListLength(list) > 0) {
		Relation relation = findListElementRelation(list);
		ASSERT(!IsNullRelation(relation))
		
		byte parameterIO[3];
		CopyBytesPermuted(
			(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT}, parameterIO, listLibrary->listRoleIndex, 3);
		Operator const * op = ServiceGetOperator(
			(Service) {.relation = relation, .ioSignature = CreateIOSignature(parameterIO, 3)}
		);
		iterator->context = OperatorCreateContext(op, iterator->queryTuple);
	}
	else
		iterator->context = 0;
}


bool ListIteratorNext(ListIterator * iterator)
{
	if(iterator->context)
		return OperatorCall(iterator->context);
	else
		return false;
}


Atom ListIteratorGetElement(ListIterator const * iterator)
{
	return iterator->queryTuple[listLibrary->listRoleIndex[2]];
}


void ListIteratorEnd(ListIterator * iterator)
{
	if(iterator->context)
		OperatorFreeContext(iterator->context);
	SetMemory(iterator, sizeof(ListIterator), 0);
}


void PrintList(Atom list)
{
	PrintCString("(");
	Relation relation = findListElementRelation(list);
	ASSERT(!IsNullRelation(relation))
	byte elementType = relation.typeSignature.atomTypes[listLibrary->listRoleIndex[2]];

	ListIterator iterator;
	ListIterate(list, &iterator);

	while(ListIteratorNext(&iterator)) {
		Atom element = ListIteratorGetElement(&iterator);
		PrintTypedAtom(CreateTypedAtom(elementType, element));
		PrintChar(' ');
	}
	ListIteratorEnd(&iterator);

	PrintChar(')');
}


void ListSetup(void)
{
	listLibrary = Allocate(sizeof(ListLibrary));
	SetPersistentState(STATE_KEY_LIST, listLibrary);

	listLibrary->listRoleName = CreateNameFromCString("list");
	listLibrary->positionRoleName = CreateNameFromCString("position");
	listLibrary->elementRoleName = CreateNameFromCString("element");
	listLibrary->lengthRoleName = CreateNameFromCString("length");

	// Create the (list position element) and (list length) forms
	listLibrary->listPredicateForm = CreatePredicateForm((
		Atom[]) {listLibrary->listRoleName, listLibrary->positionRoleName, listLibrary->elementRoleName}, 3);
	listLibrary->listRoleIndex[LIST_ROLE_LIST] = PredicateRoleIndex(listLibrary->listPredicateForm, listLibrary->listRoleName);
	listLibrary->listRoleIndex[LIST_ROLE_POSITION] = PredicateRoleIndex(listLibrary->listPredicateForm, listLibrary->positionRoleName);
	listLibrary->listRoleIndex[LIST_ROLE_ELEMENT] = PredicateRoleIndex(listLibrary->listPredicateForm, listLibrary->elementRoleName);
	listLibrary->listTermForm = CreateTermForm(listLibrary->listPredicateForm, true);

	listLibrary->listLengthPredicateForm = CreatePredicateForm((
		Atom[]) {listLibrary->listRoleName, listLibrary->lengthRoleName}, 2);
	listLibrary->listLengthRoleIndex[LIST_LENGTH_ROLE_LIST] =
		PredicateRoleIndex(listLibrary->listLengthPredicateForm, listLibrary->listRoleName);
	listLibrary->listLengthRoleIndex[LIST_LENGTH_ROLE_LENGTH] =
		PredicateRoleIndex(listLibrary->listLengthPredicateForm, listLibrary->lengthRoleName);
	listLibrary->listLengthTermForm = CreateTermForm(listLibrary->listLengthPredicateForm, true);

	NameRelease(listLibrary->lengthRoleName);
	NameRelease(listLibrary->elementRoleName);
	NameRelease(listLibrary->positionRoleName);
	NameRelease(listLibrary->listRoleName);

	// Create relations
	TypeSignature typeSignature = {0};
	// (list:ID position:INT element:ID)
	CopyBytesPermuted(
		(byte[]) {AT_ID, AT_INT, AT_ID}, typeSignature.atomTypes, listLibrary->listRoleIndex, 3);
	listLibrary->listIDRelation = (Relation) {.form = listLibrary->listTermForm, .typeSignature = typeSignature};
	listLibrary->listIDTupleStore = CreateTupleStore(listLibrary->listIDRelation, GetStorageProvider(PROVIDER_BTREE), 3, listLibrary->listRoleIndex);
	
	// (list:ID position:INT element:LETTER)
	CopyBytesPermuted(
		(byte[]) {AT_ID, AT_INT, AT_LETTER}, typeSignature.atomTypes, listLibrary->listRoleIndex, 3);
	listLibrary->listLetterRelation = (Relation) {.form = listLibrary->listTermForm, .typeSignature = typeSignature};
	listLibrary->listLetterTupleStore = CreateTupleStore(listLibrary->listLetterRelation, GetStorageProvider(PROVIDER_BTREE), 3, listLibrary->listRoleIndex);
	
	// (list:ID length:INT)
	typeSignature = (TypeSignature) {0};
	CopyBytesPermuted(
		(byte[]) {AT_ID, AT_INT}, typeSignature.atomTypes, listLibrary->listLengthRoleIndex, 2);
	listLibrary->listLengthRelation = (Relation) {.form = listLibrary->listLengthTermForm, .typeSignature = typeSignature};
	listLibrary->listLengthTupleStore = CreateTupleStore(listLibrary->listLengthRelation, GetStorageProvider(PROVIDER_BTREE), 2, listLibrary->listLengthRoleIndex);
	
	IFactRelease(listLibrary->listLengthTermForm);
	IFactRelease(listLibrary->listLengthPredicateForm);
	IFactRelease(listLibrary->listTermForm);
	IFactRelease(listLibrary->listPredicateForm);

	// Get operators
	// for (list <ID position >INT element >_)
	IOSignature elementIOSignature = {0};
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
		elementIOSignature.parameterIO, listLibrary->listRoleIndex, 3);
	listLibrary->listIDOperator = ServiceGetOperator(
		(Service) {.relation = listLibrary->listIDRelation, .ioSignature = elementIOSignature}
	);
	ASSERT(listLibrary->listIDOperator)
	listLibrary->listLetterOperator = ServiceGetOperator(
		(Service) {.relation = listLibrary->listLetterRelation, .ioSignature = elementIOSignature}
	);
	ASSERT(listLibrary->listLetterOperator)
	// for (list <ID length >INT)
	IOSignature lengthIOSignature = {0};
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_OUT},
		lengthIOSignature.parameterIO, listLibrary->listLengthRoleIndex, 2);
	listLibrary->listLengthOperator = ServiceGetOperator(
		(Service) {.relation = listLibrary->listLengthRelation, .ioSignature = lengthIOSignature}
	);
	ASSERT(listLibrary->listLengthOperator)
}


void ListRestore(void)
{
	listLibrary = GetPersistentState(STATE_KEY_LIST);
	ASSERT(listLibrary)
}


void ListShutdown(void)
{
	DropRelation(listLibrary->listLengthRelation);
	DropRelation(listLibrary->listLetterRelation);
	DropRelation(listLibrary->listIDRelation);

	Free(listLibrary);
	SetPersistentState(STATE_KEY_LIST, 0);
	listLibrary = 0;
}
