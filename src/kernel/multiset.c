
#include "kernel/UInt.h"
#include "lang/Variable.h"
#include "lang/TypedAtom.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/ServiceRegistry.h"
#include "util/sort.h"


void MultisetSetTuple(TypedTuple * tuple, TypedAtom multiset, TypedAtom element, TypedAtom multiple)
{
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET),
		multiset
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT),
		element
	);
	TypedTupleSetElement(
		tuple,
		CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE),
		multiple
	);
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
		RegistryGetCoreBTreeService(FORM_MULTISET_ELEMENT_MULTIPLE),
		CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
	);
	TypedTuple * tuple = CreateTypedTuple(3);
	for(index32 i = 0; i < nUniqueElements; i++) {
		ElementMultiple em = generator(i, data);
		MultisetSetTuple(
			tuple,
			invalidAtom, em.element, CreateTypedAtom(AT_UINT, (Atom) {._uint = em.multiple})
		);
		IFactAddClause(draft, tuple);
	}
	FreeTypedTuple(tuple);
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
	BTree * tree = RegistryGetCoreBTreeService(FORM_MULTISET_ELEMENT_MULTIPLE);
	iterator->queryTuple = CreateTypedTuple(3);
	MultisetSetTuple(
		iterator->queryTuple,
		CreateTypedAtom(AT_ID, multiset), anonymousVariable, anonymousVariable
	);
	RelationBTreeIterate(tree, iterator->queryTuple, &(iterator->treeIterator));
}


bool MultisetIteratorNext(MultisetIterator * iterator)
{
	return RelationBTreeIteratorNext(&(iterator->treeIterator));
}


ElementMultiple MultisetIteratorGetElement(MultisetIterator const * iterator)
{
	TypedTuple const * tuple = RelationBTreeIteratorPeekTuple(&(iterator->treeIterator));
	TypedAtom element = TypedTupleGetElement(
		tuple, CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)
	);
	TypedAtom multiple = TypedTupleGetElement(
		tuple,CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)
	);
	
	ElementMultiple em;
	em.element = element;
	em.multiple = (size32) multiple.atom._uint;
	return em;
}

void MultisetIteratorEnd(MultisetIterator * iterator)
{
	RelationBTreeIteratorEnd(&(iterator->treeIterator));
	FreeTypedTuple(iterator->queryTuple);
	SetMemory(iterator, sizeof(MultisetIterator), 0);
}


size32 MultisetNUniqueElements(Atom multiset)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	size32 nElements = 0;
	while(MultisetIteratorNext(&iterator)) {
		nElements++;
	}
	MultisetIteratorEnd(&iterator);
	return nElements;
}


size32 MultisetSize(Atom multiset)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	size32 size = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		size += em.multiple;
	}
	MultisetIteratorEnd(&iterator);
	return size;
}


void PrintMultiset(Atom multiset)
{
	PrintChar('{');
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		PrintTypedAtom(em.element);
		PrintF("(%u)", em.multiple);
		MultisetIteratorNext(&iterator);
	}
	MultisetIteratorEnd(&iterator);
	PrintChar('}');
}


void MultisetIterationOrder(Atom multiset, TypedAtom const * elements, index8 * order, size8 nElements)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, &iterator);
	size8 i = 0;
	while(MultisetIteratorNext(&iterator)) {
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
	}
	MultisetIteratorEnd(&iterator);
}

