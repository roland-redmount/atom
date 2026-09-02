
#include "lang/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "library/list.h"
#include "storage/RelationBTree.h"
#include "util/hashing.h"


static Atom listRoleName;
static Atom positionRoleName;
static Atom elementRoleName;
static Atom lengthRoleName;

static Atom listPredicateForm;
static Atom listTermForm;
static index8 listRoleIndex[3];

static Atom listLengthPredicateForm;
static Atom listLengthTermForm;
static index8 listLengthRoleIndex[2];

static Relation const * listIDRelation;
static RelationTable * listIDRelationTable;
static Operator * listIDOperator;

static Relation const * listLetterRelation;
static RelationTable * listLetterRelationTable;
static Operator * listLetterOperator;

static Relation const * listLengthRelation;
static RelationTable * listLengthRelationTable;
static Operator * listLengthOperator;


Atom GetListRoleName(void)
{
	return listRoleName;
}


Atom GetListPredicateForm(void)
{
	return listPredicateForm;
}


Atom GetListTermForm(void)
{
	return listTermForm;
}


Atom GetListLengthPredicateForm(void)
{
	return listLengthPredicateForm;
}


Atom GetListLengthTermForm(void)
{
	return listLengthTermForm;
}


index8 const * GetListRoleIndex(void)
{
	return listRoleIndex;
}


void ListSetTuple(Atom const inputTuple[], Atom tuple[])
{
	TupleCopyPermuted(inputTuple, tuple, listRoleIndex, 3);
}


void ListSetByteArray(byte const inputArray[], byte array[])
{
	CopyBytesPermuted(inputArray, array, listRoleIndex, 3);
}


index8 const * GetListLengthRoleIndex(void)
{
	return listLengthRoleIndex;
}


Relation const * GetListRelation(byte elementType)
{
	switch(elementType) {
	case AT_ID:
		return listIDRelation;

	case AT_LETTER:
		return listLetterRelation;

	default:
		ASSERT(false)
		return 0;
	}
}


RelationTable * GetListRelationTable(byte elementType)
{
	switch(elementType) {
	case AT_ID:
		return listIDRelationTable;

	case AT_LETTER:
		return listLetterRelationTable;

	default:
		ASSERT(false)
		return 0;
	}
}


Operator * GetListOperator(byte elementType)
{
	switch(elementType) {
	case AT_ID:
		return listIDOperator;

	case AT_LETTER:
		return listLetterOperator;

	default:
		ASSERT(false)
		return 0;
	}
}


Relation const * GetListLengthRelation(void)
{
	return listLengthRelation;
}


RelationTable * GetListLengthRelationTable(void)
{
	return listLengthRelationTable;
}


Operator * GetListLengthOperator(void)
{
	return listLengthOperator;
}


/**
 * Create an immutable list defined by an ifact,
 * storing the array of characters in a relation table.
 * 
 * TODO: the (list length) fact could be computed rather than explicit?
 * 
 * For lists defined by the below function, the elements must always be completely known
 * as they are identifying facts. They cannot be altered after the list IFact is created,
 * and so (list length) is also fixed.
 * 
 * However, the (list position element) table can also contain
 * list atoms that are not defined by this table. In this case, elements may be unknown
 * and the (list position element) table may be altered over time. We must then take
 * care to keep (list length) valid. This should be handled by logical consistency checks,
 * but those are not workable for "core" tables so we must check explicitly.
 */

Atom CreateList(ListElementGenerator generator, void const * data, byte elementType, size32 nElements)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddListToIFact(&draft, generator, data, elementType, nElements);
	return IFactEnd(&draft);
}


// assert (list length) fact
static void assertListLength(IFactDraft * draft, size32 nElements)
{
	IFactBeginConjunction(draft, listLengthRelationTable, listLengthRoleIndex[0]);

	Atom listLengthTuple[2];
	listLengthTuple[listLengthRoleIndex[1]] = (Atom) {._int = nElements};
	IFactAddTuple(draft, listLengthTuple);
	IFactEndConjunction(draft);	
}


void AddListToIFact(IFactDraft * draft, ListElementGenerator generator, void const * data, byte elementType, size32 nElements)
{
	if(nElements > 0) {
		RelationTable const * table = GetListRelationTable(elementType);
		// assert (ĺist position elements) facts for each element
		IFactBeginConjunction(draft, table, listRoleIndex[0]);
		Atom listElementTuple[3];
		for(index32 i = 0; i < nElements; i++) {
			TupleCopyPermuted(
				(Atom[]) {(Atom) {0}, (Atom) {._int = i + 1}, generator(i, data)},
				listElementTuple, listRoleIndex, 3);
			IFactAddTuple(draft, listElementTuple);
		}
		IFactEndConjunction(draft);
	}
	assertListLength(draft, nElements);
}


void ListBegin(IFactDraft * draft)
{
	IFactBegin(draft);
}


Atom ListEnd(IFactDraft * draft)
{
	size32 nElements;
	if(draft->hasBegunConjunction) {
		// end (ĺist position elements)
		nElements = IFactEndConjunction(draft);
	}
	else {
		// no elements were added, create the empty list
		nElements = 0;
	}
	assertListLength(draft, nElements);

	return IFactEnd(draft);
}


static Atom arrayElementGenerator(index32 index, void const * data)
{
	Atom const * atoms = data;
	return atoms[index];
}


Atom CreateListFromArray(Atom const atoms[], byte elementType, size8 nAtoms)
{
	return CreateList(arrayElementGenerator, atoms, elementType, nAtoms);
}


bool IsList(Atom atom)
{
	// We define this from the (list length) relation since
	// there may be no (list element position) fact if atom is an empty list.
	return AtomHasRole(atom, listLengthRelation, listRoleName);
}


size32 ListLength(Atom list)
{
	Atom arguments[2];
	arguments[listLengthRoleIndex[0]] = list;
	ASSERT(OperatorCallOnce(listLengthOperator, arguments))
	return (size32) arguments[listLengthRoleIndex[1]]._int;
}


/**
 * Determine the (list position element) relation that the given list
 * participates in, depending on its element type.
 * 
 * TODO: this is not well-defined in general, there may be > 1 relation for lists
 * containing mixed types, although CreateList() does not yields such lists.
 */
static Relation const * lookupListElementRelation(Atom list)
{
	return LookupFindRelation(list, listPredicateForm, listRoleName);
}


Atom ListGetElement(Atom list, index32 position)
{
	ASSERT(ListLength(list) > 0)
	Relation const * relation = lookupListElementRelation(list);
	ASSERT(relation)

	byte parameterIO[3];
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_IN, PARAMETER_OUT}, parameterIO, listRoleIndex, 3);
	Operator const * op = ServiceRegistryFind(
		relation, CreateIOSignature(parameterIO, 3));

	Atom arguments[3];
	arguments[listRoleIndex[0]] = list;
	arguments[listRoleIndex[1]] = (Atom) {._int = position};
	ASSERT(OperatorCallOnce(op, arguments))
	return arguments[listRoleIndex[2]];
}


index32 ListGetPosition(Atom list, Atom element)
{
	ASSERT(IsList(list))
	Relation const * relation = lookupListElementRelation(list);
	ASSERT(relation)

	// TODO: this service is not one the B-tree provider registers, as its inputs are not
	// a prefix of the index column order; see RelationTableProvider.registerServices().
	// Calling this function will trigger the ASSERT below, unless a query has already
	// compiled a FILTER service for the pattern; see compileFilterVariants() in compiler.c.
	// Asking the compiler here, or an array-based storage provider, would give one.

	byte parameterIO[3];
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_IN}, parameterIO, listRoleIndex, 3);
	Operator const * op = ServiceRegistryFind(
		relation, CreateIOSignature(parameterIO, 3));

	Atom arguments[3];
	arguments[listRoleIndex[0]] = list;
	arguments[listRoleIndex[2]] = element;
	ASSERT(OperatorCallOnce(op, arguments))
	return arguments[listRoleIndex[1]]._int;
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


void CopyListToTuple(Atom list, TypedTuple * tuple)
{
	ASSERT(ListLength(list) == tuple->nAtoms)
	Relation const * relation = lookupListElementRelation(list);
	byte elementType = relation->typeSignature.atomTypes[listRoleIndex[2]];
	ListIterator iterator;
	ListIterate(list, &iterator);
	index8 i = 0;
	while(ListIteratorNext(&iterator)) {
		Atom element = ListIteratorGetElement(&iterator);
		TypedTupleSetElement(tuple, i, CreateTypedAtom(elementType, element));
		i++;
	}
	ListIteratorEnd(&iterator);
}


void ListIterate(Atom list, ListIterator * iterator)
{
	iterator->queryTuple[listRoleIndex[0]] = list;

	if(ListLength(list) > 0) {
		Relation const * relation = lookupListElementRelation(list);
		ASSERT(relation)
		
		byte parameterIO[3];
		CopyBytesPermuted(
			(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT}, parameterIO, listRoleIndex, 3);
		Operator const * op = ServiceRegistryFind(
			relation, CreateIOSignature(parameterIO, 3));
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
	return iterator->queryTuple[listRoleIndex[2]];
}


void ListIteratorEnd(ListIterator * iterator)
{
	if(iterator->context)
		OperatorFreeContext(iterator->context);
	SetMemory(iterator, sizeof(ListIterator), 0);
}


void PrintList(Atom list)
{
	PrintCString("LIST{");
	Relation const * relation = lookupListElementRelation(list);
	ASSERT(relation)
	byte elementType = relation->typeSignature.atomTypes[listRoleIndex[2]];

	ListIterator iterator;
	ListIterate(list, &iterator);

	while(ListIteratorNext(&iterator)) {
		Atom element = ListIteratorGetElement(&iterator);
		PrintTypedAtom(CreateTypedAtom(elementType, element));
		PrintChar(' ');
	}
	ListIteratorEnd(&iterator);

	PrintChar('}');
}


void ListSetup(void)
{
	listRoleName = CreateNameFromCString("list");
	positionRoleName = CreateNameFromCString("position");
	elementRoleName = CreateNameFromCString("element");
	lengthRoleName = CreateNameFromCString("length");

	// Create the (list position element) and (list length) forms
	listPredicateForm = CreatePredicateForm((
		Atom[]) {listRoleName, positionRoleName, elementRoleName}, 3);
	listRoleIndex[LIST_ROLE_LIST] = PredicateRoleIndex(listPredicateForm, listRoleName);
	listRoleIndex[LIST_ROLE_POSITION] = PredicateRoleIndex(listPredicateForm, positionRoleName);
	listRoleIndex[LIST_ROLE_ELEMENT] = PredicateRoleIndex(listPredicateForm, elementRoleName);
	listTermForm = CreateTermForm(listPredicateForm, true);

	listLengthPredicateForm = CreatePredicateForm((
		Atom[]) {listRoleName, lengthRoleName}, 2);
	listLengthRoleIndex[LIST_LENGTH_ROLE_LIST] =
		PredicateRoleIndex(listLengthPredicateForm, listRoleName);
	listLengthRoleIndex[LIST_LENGTH_ROLE_LENGTH] =
		PredicateRoleIndex(listLengthPredicateForm, lengthRoleName);
	listLengthTermForm = CreateTermForm(listLengthPredicateForm, true);

	NameRelease(lengthRoleName);
	NameRelease(elementRoleName);
	NameRelease(positionRoleName);
	NameRelease(listRoleName);

	// Create relations
	TypeSignature typeSignature = {0};
	// (list:ID position:INT element:ID)
	CopyBytesPermuted(
		(byte[]) {AT_ID, AT_INT, AT_ID}, typeSignature.atomTypes, listRoleIndex, 3);
	listIDRelation = CreateRelation(listTermForm, 3, typeSignature);
	// (list:ID position:INT element:LETTER)
	CopyBytesPermuted(
		(byte[]) {AT_ID, AT_INT, AT_LETTER}, typeSignature.atomTypes, listRoleIndex, 3);
	listLetterRelation = CreateRelation(listTermForm, 3, typeSignature);
	// (list:ID length:INT)
	typeSignature = (TypeSignature) {0};
	CopyBytesPermuted(
		(byte[]) {AT_ID, AT_INT}, typeSignature.atomTypes, listLengthRoleIndex, 2);
	listLengthRelation = CreateRelation(listLengthTermForm, 2, typeSignature);

	IFactRelease(listLengthTermForm);
	IFactRelease(listLengthPredicateForm);
	IFactRelease(listTermForm);
	IFactRelease(listPredicateForm);

	// Create relation tables
	// (list:ID position:INT element:ID)
	listIDRelationTable = CreateRelationTable(listIDRelation, &btreeTableProvider, listRoleIndex);
	// (list:ID position:INT element:LETTER)
	listLetterRelationTable = CreateRelationTable(listLetterRelation, &btreeTableProvider, listRoleIndex);
	// (list:ID length:INT)
	listLengthRelationTable = CreateRelationTable(
		listLengthRelation, &btreeTableProvider, listLengthRoleIndex);

	ReleaseRelation(listLengthRelation);
	ReleaseRelation(listLetterRelation);
	ReleaseRelation(listIDRelation);

	// Get operators
	// for (list <ID position >INT element >_)
	IOSignature elementIOSignature = {0};
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
		elementIOSignature.parameterIO, listRoleIndex, 3);
	listIDOperator = ServiceRegistryFind(listIDRelation, elementIOSignature);
	ASSERT(listIDOperator)
	listLetterOperator = ServiceRegistryFind(listLetterRelation, elementIOSignature);
	ASSERT(listLetterOperator)
	// for (list <ID length >INT)
	IOSignature lengthIOSignature = {0};
	CopyBytesPermuted(
		(byte[]) {PARAMETER_IN, PARAMETER_OUT},
		lengthIOSignature.parameterIO, listLengthRoleIndex, 2);
	listLengthOperator = ServiceRegistryFind(listLengthRelation, lengthIOSignature);
	ASSERT(listLengthOperator)
}


void ListShutdown(void)
{
	ReleaseRelationTable(listLengthRelationTable);
	ReleaseRelationTable(listLetterRelationTable);
	ReleaseRelationTable(listIDRelationTable);
}
