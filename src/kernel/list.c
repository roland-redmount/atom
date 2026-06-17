
#include "kernel/UInt.h"
#include "lang/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/list.h"
#include "kernel/kernel.h"
#include "kernel/ServiceRegistry.h"
#include "lang/AtomType.h"
#include "lang/PredicateForm.h"
#include "util/hashing.h"


/**
 * Assign values to a tuple from the (list position element) relation
 */
void ListSetTuple(TypedTuple * tuple, TypedAtom list, TypedAtom position, TypedAtom element)
{
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST),
		list
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_POSITION),
		position
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT),
		element
	);
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

Atom CreateList(ListElementGenerator generator, void const * data, size32 nElements)
{
	IFactDraft draft;
	IFactBegin(&draft);
	AddListToIFact(&draft, generator, data, nElements);
	return IFactEnd(&draft);
}


// assert (list length) fact
static void assertListLength(IFactDraft * draft, size32 nElements)
{
	Atom listLengthForm = GetCorePredicateForm(FORM_LIST_LENGTH);

	IFactBeginPredicateForm(
		draft,
		listLengthForm, 
		RegistryGetCoreBTreeService(FORM_LIST_LENGTH),
		CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LIST)
	);

	TypedTuple * listLengthTuple = CreateTypedTuple(2);
	ListLengthSetTuple(
		listLengthTuple, invalidAtom, CreateTypedAtom(AT_UINT, (Atom) {._uint = nElements}));
	IFactAddTuple(draft, listLengthTuple);
	FreeTypedTuple(listLengthTuple);
	IFactEndPredicateForm(draft);	
}


void AddListToIFact(IFactDraft * draft, ListElementGenerator generator, void const * data, size32 nElements)
{
	if(nElements > 0) {
		// assert (ĺist position elements) facts for each element
		IFactBeginPredicateForm(
			draft,
			GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT),
			RegistryGetCoreBTreeService(FORM_LIST_POSITION_ELEMENT),
			CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST)
		);

		TypedTuple * listElementTuple = CreateTypedTuple(3);
		for(index32 i = 0; i < nElements; i++) {
			ListSetTuple(
				listElementTuple,
				invalidAtom, CreateTypedAtom(AT_UINT, (Atom) {._uint = i + 1}), generator(i, data)
			);
			IFactAddTuple(draft, listElementTuple);
		}
		FreeTypedTuple(listElementTuple);
		IFactEndPredicateForm(draft);
	}
	assertListLength(draft, nElements);
}


void ListBegin(IFactDraft * draft)
{
	IFactBegin(draft);
	// we postpone starting  ()
}


index32 ListAddElement(IFactDraft * draft, TypedAtom element)
{
	if(!draft->hasBegunConjunction) {
		// first element, begin (ĺist position elements)
		IFactBeginPredicateForm(
			draft,
			GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT),
			RegistryGetCoreBTreeService(FORM_LIST_POSITION_ELEMENT),
			CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST)
		);
	}

	TypedTuple * listElementTuple = CreateTypedTuple(3);
	index32 position = IFactDraftCurrentNTuples(draft) + 1;
	ListSetTuple(
		listElementTuple,
		invalidAtom, CreateTypedAtom(AT_UINT, (Atom) {._uint = position}), element
	);
	IFactAddTuple(draft, listElementTuple);
	FreeTypedTuple(listElementTuple);
	return position;
}


Atom ListEnd(IFactDraft * draft)
{
	size32 nElements;
	if(draft->hasBegunConjunction) {
		// end (ĺist position elements)
		nElements = IFactEndPredicateForm(draft);
	}
	else {
		// no elements were added, create the empty list
		nElements = 0;
	}
	assertListLength(draft, nElements);

	return IFactEnd(draft);
}


void ListLengthSetTuple(TypedTuple * tuple, TypedAtom list, TypedAtom length)
{
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LIST),
		list
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LENGTH),
		length
	);
}


static TypedAtom arrayElementGenerator(index32 index, void const * data)
{
	TypedAtom const * atoms = (TypedAtom const *) data;
	return atoms[index];
}


Atom CreateListFromArray(TypedAtom const * atoms, size8 nAtoms)
{
	return CreateList(arrayElementGenerator, atoms, nAtoms);
}


static TypedAtom tupleElementGenerator(index32 index, void const * data)
{
	TypedTuple const * tuple = (TypedTuple const *) data;
	return TypedTupleGetElement(tuple, index);
}


Atom CreateListFromTuple(TypedTuple const * tuple)
{
	return CreateList(tupleElementGenerator, tuple, tuple->nAtoms);
}


bool IsList(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_LIST_LENGTH),
		GetCoreRoleName(ROLE_LIST)
	);
}


size32 ListLength(Atom list)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_LIST_LENGTH);

	TypedTuple * queryTuple = CreateTypedTuple(2);
	ListLengthSetTuple(queryTuple, CreateTypedAtom(AT_ID, list), anonymousVariable);
	TypedAtom length = RelationBTreeQuerySingleAtom(
		tree, queryTuple,
		CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LENGTH)
	);
	FreeTypedTuple(queryTuple);
	return (size32) length.atom._uint;
}


TypedAtom ListGetElement(Atom list, index32 position)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_LIST_POSITION_ELEMENT);

	TypedTuple * queryTuple = CreateTypedTuple(3);
	ListSetTuple(
		queryTuple,
		CreateTypedAtom(AT_ID, list), CreateTypedAtom(AT_UINT, (Atom) {._uint = position}), anonymousVariable
	);
	TypedAtom element = RelationBTreeQuerySingleAtom(
		tree, queryTuple,
		CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)
	);
	FreeTypedTuple(queryTuple);
	return element;
}


void ListGetElementsArray(Atom list, TypedAtom * elements)
{
	ASSERT(IsList(list))
	ListIterator iterator;
	ListIterate(list, &iterator);
	
	for(index32 i = 0; ListIteratorNext(&iterator); i++) {
		elements[i] = ListIteratorGetElement(&iterator);
	}
	ListIteratorEnd(&iterator);
}


index32 ListGetPosition(Atom list, TypedAtom element)
{
	ASSERT(IsList(list))
	BTree * tree = RegistryGetCoreBTreeService(FORM_LIST_POSITION_ELEMENT);

	TypedTuple * queryTuple = CreateTypedTuple(3);
	ListSetTuple(queryTuple, CreateTypedAtom(AT_ID, list), anonymousVariable, element);

	RelationBTreeIterator iterator;
	RelationBTreeIterate(tree, queryTuple, &iterator);
	
	index32 p = 0;
	if(RelationBTreeIteratorNext(&iterator)) {
		TypedAtom position = RelationBTreeIteratorGetAtom(
			&iterator,
			CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_POSITION)
		);
		p = (index32) position.atom._uint;
	}
	RelationBTreeIteratorEnd(&iterator);
	FreeTypedTuple(queryTuple);
	return p;
}


// lexical ordering of two lists
// NOTE: it is currently not possible to use this
// in the CompareTypedAtoms() function for
// canonical ordering of list (and string) atoms
// since this function depends on B-tree iteration,
// which leads to infinite recursion when comparing B-tree ḱeys
int8 ListLexicalOrdering(Atom list1, Atom list2, int8 (*compare)(TypedAtom, TypedAtom))
{
	if(list1.hash == list2.hash)
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
		TypedAtom atom1 = ListIteratorGetElement(&iterator1);
		TypedAtom atom2 = ListIteratorGetElement(&iterator2);
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

	ListIterator iterator;
	ListIterate(list, &iterator);
	index8 i = 0;
	while(ListIteratorNext(&iterator)) {
		TypedTupleSetElement(tuple, i, ListIteratorGetElement(&iterator));
		i++;
	}
	ListIteratorEnd(&iterator);
}


/**
 * List iterator
 * 
 * This is a thin wrapper around RelationBTreeIterator.
 */

void ListIterate(Atom list, ListIterator * iterator)
{
	BTree * tree = RegistryGetCoreBTreeService(FORM_LIST_POSITION_ELEMENT);
	iterator->queryTuple = CreateTypedTuple(3);
	ListSetTuple(iterator->queryTuple, CreateTypedAtom(AT_ID, list), anonymousVariable, anonymousVariable);
	RelationBTreeIterate(tree, iterator->queryTuple, &(iterator->treeIterator));
}


bool ListIteratorNext(ListIterator * iterator)
{
	return RelationBTreeIteratorNext(&(iterator->treeIterator));
}


TypedAtom ListIteratorGetElement(ListIterator const * iterator)
{
	TypedTuple const * tuple = RelationBTreeIteratorPeekTuple(&(iterator->treeIterator));
	return TypedTupleGetElement(tuple, CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT));
}


void ListIteratorEnd(ListIterator * iterator)
{
	RelationBTreeIteratorEnd(&(iterator->treeIterator));
	FreeTypedTuple(iterator->queryTuple);
}


void PrintList(Atom list)
{
	PrintCString("LIST{");

	ListIterator iterator;
	ListIterate(list, &iterator);

	while(ListIteratorNext(&iterator)) {
		TypedAtom element = ListIteratorGetElement(&iterator);
		PrintTypedAtom(element);
		PrintChar(' ');
	}
	ListIteratorEnd(&iterator);

	PrintChar('}');
}
