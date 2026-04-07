
#include "datumtypes/id.h"
#include "datumtypes/UInt.h"
#include "datumtypes/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/list.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/DatumType.h"
#include "lang/PredicateForm.h"
#include "util/hashing.h"


/**
 * Assign values to a tuple from the (list position element) relation
 */
static void listSetTuple(Atom * tuple, Atom list, Atom position, Atom element)
{
	tuple[CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST)] = list;
	tuple[CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_POSITION)] = position;
	tuple[CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)] = element;
}


/**
 * Create a list (immutable) backed by an ifact,
 * storing the array of characters in a relation table
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

Datum CreateList(ListElementGenerator generator, void const * data, size32 nElements)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddListToIFact(&draft, generator, data, nElements);
	return IFactEnd(&draft);
}


// assert (list length) fact
static void assertListLength(IFactDraft * draft, size32 nElements)
{
	Datum listLengthForm = GetCorePredicateForm(FORM_LIST_LENGTH);

	IFactBeginConjunction(
		draft, listLengthForm, 
		CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LIST)
	);

	Atom listLengthTuple[2];
	ListLengthSetTuple(listLengthTuple, invalidAtom, CreateUInt(nElements));
	IFactAddClause(draft, listLengthTuple);
	IFactEndConjunction(draft);	
}


void AddListToIFact(IFactDraft * draft, ListElementGenerator generator, void const * data, size32 nElements)
{
	if(nElements > 0) {
		// assert (ĺist position elements) facts for each element
		IFactBeginConjunction(
			draft,
			GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT),
			CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST)
		);

		Atom listElementTuple[3];
		for(index32 i = 0; i < nElements; i++) {
			listSetTuple(listElementTuple, invalidAtom, CreateUInt(i + 1), generator(i, data));
			IFactAddClause(draft, listElementTuple);
		}
		IFactEndConjunction(draft);
	}
	assertListLength(draft, nElements);
}


void ListBegin(IFactDraft * draft)
{
	IFactBegin(draft);
	// we postpone starting  ()
}


index32 ListAddElement(IFactDraft * draft, Atom element)
{
	if(!draft->hasBegunConjunction) {
		// first element, begin (ĺist position elements)
		IFactBeginConjunction(
			draft,
			GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT),
			CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST)
		);
	}

	Atom listElementTuple[3];
	index32 position = IFactDraftCurrentNClauses(draft) + 1;
	listSetTuple(listElementTuple, invalidAtom, CreateUInt(position), element);
	IFactAddClause(draft, listElementTuple);
	return position;
}


Datum ListEnd(IFactDraft * draft)
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


void ListLengthSetTuple(Atom * tuple, Atom list, Atom length)
{
	tuple[CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LIST)] = list;
	tuple[CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LENGTH)] = length;
}


Atom arrayElementGenerator(index32 index, void const * data)
{
	Atom const * atoms = (Atom const *) data;
	return atoms[index];
}


Datum CreateListFromArray(Atom const * atoms, size8 nAtoms)
{
	return CreateList(arrayElementGenerator, atoms, nAtoms);
}


bool IsList(Datum atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_LIST_LENGTH),
		GetCoreRoleName(ROLE_LIST)
	);
}


size32 ListLength(Datum list)
{
	BTree * tree = RegistryGetCoreTable(FORM_LIST_LENGTH);

	Atom queryTuple[2];
	ListLengthSetTuple(queryTuple, CreateID(list), anonymousVariable);
	Atom resultTuple[2];
	RelationBTreeQuerySingle(tree, queryTuple, resultTuple);
	Atom length = resultTuple[CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LENGTH)];
	return (size32) GetUIntValue(length);
}


Atom ListGetElement(Datum list, index32 position)
{
	BTree * tree = RegistryGetCoreTable(FORM_LIST_POSITION_ELEMENT);

	Atom queryTuple[3];
	listSetTuple(queryTuple, CreateID(list), CreateUInt(position), anonymousVariable);
	Atom resultTuple[3];
	RelationBTreeQuerySingle(tree, queryTuple, resultTuple);
	return resultTuple[CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)];
}


void ListGetElementsArray(Datum list, Atom * elements)
{
	ASSERT(IsList(list))
	ListIterator iterator;
	ListIterate(list, &iterator);
	
	for(index32 i = 0; ListIteratorHasNext(&iterator); i++) {
		elements[i] = ListIteratorGetElement(&iterator);
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);
}


index32 ListGetPosition(Datum list, Atom element)
{
	ASSERT(IsList(list))
	BTree * tree = RegistryGetCoreTable(FORM_LIST_POSITION_ELEMENT);

	Atom queryTuple[3];
	listSetTuple(queryTuple, CreateID(list), anonymousVariable, element);

	RelationBTreeIterator iterator;
	RelationBTreeIterate(tree, queryTuple, &iterator);
	
	index32 p = 0;
	if(RelationBTreeIteratorHasTuple(&iterator)) {
		Atom position = RelationBTreeIteratorGetAtom(
			&iterator,
			CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_POSITION)
		);
		p = GetUIntValue(position);
	}
	RelationBTreeIteratorEnd(&iterator);
	return p;
}


// lexical ordering of two lists
// NOTE: it is currently not possible to use this
// in the CompareAtoms() function for
// canonical ordering of list (and string) datums
// since this function depends on B-tree iteration,
// which leads to infinite recursion when comparing B-tree ḱeys
int8 ListLexicalOrdering(Datum list1, Datum list2)
{
	if(list1 == list2)
		return 0;

	ListIterator iterator1;
	ListIterate(list1, &iterator1);
	ListIterator iterator2;
	ListIterate(list2, &iterator2);

	int8 listOrder = 0;
	while(true) {
		bool hasNext1 = ListIteratorHasNext(&iterator1);
		bool hasNext2 = ListIteratorHasNext(&iterator2);
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
		int8 letterOrder = CompareAtoms(atom1, atom2);
		if(letterOrder != 0) {
			listOrder = letterOrder;
			break;
		}
		ListIteratorNext(&iterator1);
		ListIteratorNext(&iterator2);
	}
	ListIteratorEnd(&iterator1);
	ListIteratorEnd(&iterator2);
	ASSERT(listOrder != 0);	// distinct, unique strings cannot be equal
	return listOrder;
}


/**
 * List iterator
 * 
 * This is a thin wrapper around RelationBTreeIterator.
 */

void ListIterate(Datum list, ListIterator * iterator)
{
	BTree * tree = RegistryGetCoreTable(FORM_LIST_POSITION_ELEMENT);
	listSetTuple(iterator->queryTuple, CreateID(list), anonymousVariable, anonymousVariable);
	RelationBTreeIterate(tree, iterator->queryTuple, &(iterator->treeIterator));
}


bool ListIteratorHasNext(ListIterator const * iterator)
{
	return RelationBTreeIteratorHasTuple(&(iterator->treeIterator));
}


Atom ListIteratorGetElement(ListIterator const * iterator)
{
	Atom resultTuple[3];
	RelationBTreeIteratorGetTuple(&(iterator->treeIterator), resultTuple);
	return resultTuple[CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)];
}


void ListIteratorNext(ListIterator * iterator)
{
	RelationBTreeIteratorNext(&(iterator->treeIterator));
}


void ListIteratorEnd(ListIterator * iterator)
{
	RelationBTreeIteratorEnd(&(iterator->treeIterator));
}


void PrintList(Datum list)
{
	PrintChar('{');

	ListIterator iterator;
	ListIterate(list, & iterator);

	while(ListIteratorHasNext(&iterator)) {
		Atom element = ListIteratorGetElement(&iterator);
		PrintAtom(element);
		PrintChar(' ');
		ListIteratorNext(&iterator);
	}
	ListIteratorEnd(&iterator);

	PrintChar('}');
}
