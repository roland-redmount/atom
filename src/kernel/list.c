
#include "kernel/UInt.h"
#include "lang/Variable.h"
#include "kernel/dispatch.h"
#include "kernel/letter.h"
#include "kernel/lookup.h"
#include "kernel/list.h"
#include "kernel/kernel.h"
#include "kernel/Parameter.h"
#include "kernel/RelationTable.h"
#include "kernel/RelationRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "lang/AtomType.h"
#include "lang/PredicateForm.h"
#include "util/hashing.h"


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
	RelationTable const * listLengthTable = GetCoreRelationTable(RELATION_LIST_LENGTH);

	IFactBeginConjunction(
		draft,
		listLengthTable,
		CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LIST)
	);

	Atom listLengthTuple[2];
	CoreFormSetTuple(
		FORM_LIST_LENGTH,
		(Atom[]) {(Atom) {0}, (Atom) {._uint = nElements}},
		listLengthTuple
	);
	IFactAddTuple(draft, listLengthTuple);
	IFactEndConjunction(draft);	
}


void AddListToIFact(IFactDraft * draft, ListElementGenerator generator, void const * data, byte elementType, size32 nElements)
{
	if(nElements > 0) {
		byte atomTypes[3];
		CoreFormSetByteArray(
			FORM_LIST_POSITION_ELEMENT,
			(byte[]) {AT_ID, AT_UINT, elementType},
			atomTypes
		);
		RelationTable const * table = RelationRegistryFind(
			GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT),
			3, atomTypes
		);
		ASSERT(table);
		// assert (ĺist position elements) facts for each element
		IFactBeginConjunction(
			draft,
			table,
			CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_LIST)
		);

		Atom listElementTuple[3];
		for(index32 i = 0; i < nElements; i++) {
			CoreFormSetTuple(
				FORM_LIST_POSITION_ELEMENT,
				(Atom[]) {(Atom) {0}, (Atom) {._uint = i + 1}, generator(i, data)},
				listElementTuple
			);
			IFactAddTuple(draft, listElementTuple);
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


Atom CreateListFromArray(Atom const * atoms, byte elementType, size8 nAtoms)
{
	return CreateList(arrayElementGenerator, atoms, elementType, nAtoms);
}


bool IsList(Atom atom)
{
	// We define this from the (list length) relation since
	// there may be no (list element position) fact if atom is an empty list.
	return AtomHasRole(
		atom,
		GetCoreRelationTable(RELATION_LIST_LENGTH),
		GetCoreRoleName(ROLE_LIST)
	);
}


size32 ListLength(Atom list)
{
	Service const * service = GetCoreService(SERVICE_LIST_LENGTH);

	Atom arguments[2];
	CoreFormSetTuple(
		FORM_LIST_LENGTH,
		(Atom[]) {list, (Atom) {0}},
		arguments
	);
	ASSERT(ServiceCallOnce(service, arguments))
	return (size32) arguments[CorePredicateRoleIndex(FORM_LIST_LENGTH, ROLE_LENGTH)]._uint;
}


static RelationTable const * lookupListElementRelation(Atom list)
{
	return LookupFindRelation(
		list,
		GetCorePredicateForm(FORM_LIST_POSITION_ELEMENT),
		GetCoreRoleName(ROLE_LIST)
	);
}


Atom ListGetElement(Atom list, index32 position)
{
	ASSERT(ListLength(list) > 0)
	RelationTable const * relation = lookupListElementRelation(list);
	ASSERT(relation)

	byte parameterIO[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(byte[]) {PARAMETER_IN, PARAMETER_IN, PARAMETER_OUT},
		parameterIO
	);
	Service const * service = ServiceRegistryFind(relation, parameterIO);

	Atom arguments[3];
	CoreFormSetTuple(
		FORM_LIST_POSITION_ELEMENT,
		(Atom []) {list, (Atom) {._uint = position}, (Atom) {0}},
		arguments
	);
	ASSERT(ServiceCallOnce(service, arguments))
	return arguments[CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)];
}


index32 ListGetPosition(Atom list, Atom element)
{
	ASSERT(IsList(list))
	RelationTable const * relation = lookupListElementRelation(list);
	ASSERT(relation)

	// TODO: this service is not provided by the B-tree relation provider
	// as the keys are not in leading columns. Calling this function will
	// trigger the ASSERT below.
	byte parameterIO[3];
	CoreFormSetByteArray(
		FORM_LIST_POSITION_ELEMENT,
		(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_IN},
		parameterIO
	);
	Service const * service = ServiceRegistryFind(relation, parameterIO);
	ASSERT(service)

	Atom arguments[3];
	CoreFormSetTuple(
		FORM_LIST_POSITION_ELEMENT,
		(Atom []) {list, (Atom) {0}, element},
		arguments
	);
	ASSERT(ServiceCallOnce(service, arguments))
	return arguments[CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_POSITION)]._uint;
}


int8 ListLexicalOrdering(Atom list1, Atom list2, int8 (*compare)(Atom, Atom))
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
	RelationTable const * relation = lookupListElementRelation(list);
	byte elementType = relation->atomTypes[
		CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)
	];
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
	CoreFormSetTuple(
		FORM_LIST_POSITION_ELEMENT,
		(Atom[]) {list, (Atom) {0}, (Atom) {0}},
		iterator->queryTuple
	);

	if(ListLength(list) > 0) {
		RelationTable const * relation = lookupListElementRelation(list);
		ASSERT(relation)
		
		byte parameterIO[3];
		CoreFormSetByteArray(
			FORM_LIST_POSITION_ELEMENT,
			(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
			parameterIO
		);
		Service const * service = ServiceRegistryFind(relation, parameterIO);
		iterator->context = ServiceCreateContext(service, iterator->queryTuple);
	}
	else
		iterator->context = 0;
}


bool ListIteratorNext(ListIterator * iterator)
{
	if(iterator->context)
		return ServiceCall(iterator->context);
	else
		return false;
}


Atom ListIteratorGetElement(ListIterator const * iterator)
{
	return iterator->queryTuple[
		CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)
	];
}


void ListIteratorEnd(ListIterator * iterator)
{
	if(iterator->context)
		ServiceFreeContext(iterator->context);
	SetMemory(iterator, sizeof(ListIterator), 0);
}


void PrintList(Atom list)
{
	PrintCString("LIST{");
	RelationTable const * relation = lookupListElementRelation(list);
	ASSERT(relation)
	byte elementType = relation->atomTypes[
		CorePredicateRoleIndex(FORM_LIST_POSITION_ELEMENT, ROLE_ELEMENT)
	];

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
