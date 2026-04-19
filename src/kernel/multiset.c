
#include "datumtypes/UInt.h"
#include "datumtypes/Variable.h"
#include "lang/TypedAtom.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "util/sort.h"


void MultisetSetTuple(TypedAtom * tuple, TypedAtom multiset, TypedAtom element, TypedAtom multiple)
{
	tuple[CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)] = multiset;
	tuple[CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)] = element;
	tuple[CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)] = multiple;
}


Atom CreateMultiset(MultisetElementGenerator generator, void const * data, size32 nUniqueElements)
{
	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFact(&draft, generator, data, nUniqueElements);
	
	return IFactEnd(&draft);
}


void AddMultisetToIFact(IFactDraft * draft, MultisetElementGenerator generator, void const * data, size32 nUniqueElements)
{
	// assert (multiset element multiple) facts
	IFactBeginConjunction(
		draft, 
		GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE),
		RegistryGetCoreTable(FORM_MULTISET_ELEMENT_MULTIPLE),
		CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
	);
	TypedAtom tuple[3];
	for(index32 i = 0; i < nUniqueElements; i++) {
		ElementMultiple em = generator(i, data);
		MultisetSetTuple(tuple, invalidAtom, em.element, CreateUInt(em.multiple));
		IFactAddClause(draft, tuple);
	}
	IFactEndConjunction(draft);
}


typedef struct {
	TypedAtom const * atoms;
	size32 const * multiples;
} MultisetElementData;


static ElementMultiple arrayElementGenerator(index32 index, void const * data)
{
	MultisetElementData const * elementData = data;
	ElementMultiple em;
	em.element = elementData->atoms[index];
	em.multiple = elementData->multiples[index];
	return em;
}


Atom CreateMultisetFromArrays(TypedAtom const * atoms, size32 const * multiples, size32 nUniqueElements)
{
	MultisetElementData elementData;
	elementData.atoms = atoms;
	elementData.multiples = multiples;

	return CreateMultiset(&arrayElementGenerator, &elementData, nUniqueElements);
}


void AddMultisetToIFactFromArrays(IFactDraft * draft, TypedAtom const * atoms, size32 const * multiples, size32 nUniqueElements)
{
	MultisetElementData elementData;
	elementData.atoms = atoms;
	elementData.multiples = multiples;

	AddMultisetToIFact(draft, &arrayElementGenerator, &elementData, nUniqueElements);
}

bool IsMultiset(Atom atom)
{
	return AtomHasRole(
		atom,
		GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE),
		GetCoreRoleName(ROLE_MULTISET)
	);
}

size32 MultisetGetElementMultiple(Atom multiset, TypedAtom element)
{
	// TODO
	ASSERT(false);
	return 0;
}


/**
 * Multiset iterator
 */

void MultisetIterate(Atom multiset, MultisetIterator * iterator)
{
	BTree * tree = RegistryGetCoreTable(FORM_MULTISET_ELEMENT_MULTIPLE);
	MultisetSetTuple(iterator->queryTuple, CreateTypedAtom(AT_ID, multiset), anonymousVariable, anonymousVariable);
	RelationBTreeIterate(tree, iterator->queryTuple, &(iterator->treeIterator));
}


bool MultisetIteratorHasNext(MultisetIterator const * iterator)
{
	return RelationBTreeIteratorHasTuple(&(iterator->treeIterator));
}


void MultisetIteratorNext(MultisetIterator * iterator)
{
	RelationBTreeIteratorNext(&(iterator->treeIterator));
}


ElementMultiple MultisetIteratorGetElement(MultisetIterator const * iterator)
{
	TypedAtom resultTuple[3];
	RelationBTreeIteratorGetTuple(&(iterator->treeIterator), resultTuple);
	index8 roleElementIndex = CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT);
	index8 roleMultipleIndex = CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE);
	
	ElementMultiple em;
	em.element = resultTuple[roleElementIndex];
	em.multiple = resultTuple[roleMultipleIndex].atom;
	return em;
}

void MultisetIteratorEnd(MultisetIterator * iterator)
{
	RelationBTreeIteratorEnd(&(iterator->treeIterator));
}


size32 MultisetNUniqueElements(Atom multiset)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	size32 nElements = 0;
	while(MultisetIteratorHasNext(&iterator)) {
		nElements++;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	return nElements;
}


size32 MultisetSize(Atom multiset)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	size32 size = 0;
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		size += em.multiple;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	return size;
}


void PrintMultiset(Atom multiset)
{
	PrintChar('{');
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		PrintTypedAtom(em.element);
		PrintF("(%u)", em.multiple);
		MultisetIteratorNext(&iterator);
		if(MultisetIteratorHasNext(&iterator))
			PrintChar(' ');
	}
	MultisetIteratorEnd(&iterator);
	PrintChar('}');
}


void MultisetIterationOrder(Atom multiset, TypedAtom const * elements, index8 * order, size8 nElements)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	size8 i = 0;
	while(MultisetIteratorHasNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		index8 m = 0;
		// find corresponding element in the elements array
		for(index8 j = 0; j < nElements; j++) {
			if(SameTypedAtoms(elements[j], em.element)) {
				order[i + m] = j;
				m++;
			}
		}
		// verify that we found all multiples in array
		ASSERT(m == em.multiple);
		i += m;
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
}

