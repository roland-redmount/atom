
#include "lang/Variable.h"
#include "lang/TypedAtom.h"
#include "kernel/lookup.h"
#include "kernel/kernel.h"
#include "kernel/multiset.h"
#include "kernel/Parameter.h"
#include "kernel/RelationTableRegistry.h"
#include "kernel/ServiceRegistry.h"
#include "util/sort.h"


Atom CreateMultiset(MultisetElementGenerator generator, void const * data, size32 nUniqueElements, byte elementType)
{
	IFactDraft draft;
	IFactBegin(&draft);

	AddMultisetToIFact(&draft, generator, data, nUniqueElements, elementType);
	
	return IFactEnd(&draft);
}


/**
 * Find the relation (multiset m element e multiple n) where
 * e has the given atom type. Currently we only support multisets of ID or NAME atoms.
 */
Relation const * findMultisetRelation(byte elementType)
{
	switch(elementType) {
		case AT_ID:
		return GetCoreRelation(RELATION_MULTISET_ID);

		case AT_NAME:
		return GetCoreRelation(RELATION_MULTISET_NAME);

		default:
		ASSERT(false)
		return 0;
	}
}


/**
 * Find the tuple storage of the multiset relation for the given element atom type.
 */
static RelationTable * findMultisetTable(byte elementType)
{
	return RelationTableRegistryFind(findMultisetRelation(elementType));
}


/**
 * Find the relation associated with a multiset.
 */
static Relation const * lookupMultisetRelation(Atom multiset)
{
	return LookupFindRelation(
		multiset,
		GetCorePredicateForm(FORM_MULTISET_ELEMENT_MULTIPLE),
		GetCoreRoleName(ROLE_MULTISET)
	);
}


/**
 * Find the atom type of the elements of an existing multiset.
 */
static byte findMultisetElementType(Atom multiset)
{
	Relation const * relation = lookupMultisetRelation(multiset);
	return relation->typeSignature.atomTypes[
		CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)
	];
}


void AddMultisetToIFact(
	IFactDraft * draft,
	MultisetElementGenerator generator, void const * data, size32 nUniqueElements, byte elementType)
{
	RelationTable * table = findMultisetTable(elementType);

	// assert (multiset element multiple) facts
	IFactBeginConjunction(
		draft, 
		table,
		CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTISET)
	);
	Atom tuple[3];
	for(index32 i = 0; i < nUniqueElements; i++) {
		ElementMultiple em = generator(i, data);
		CoreFormSetTuple(
			FORM_MULTISET_ELEMENT_MULTIPLE,
			(Atom[]) {
				(Atom) {0},
				em.element,
				(Atom) {._int = em.multiple},
			},
			tuple
		);
		IFactAddTuple(draft, tuple);
	}
	IFactEndConjunction(draft);
}


typedef struct {
	Atom const * atoms;
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


Atom CreateMultisetFromArrays(Atom const atoms[], size32 const multiples[], size32 nUniqueElements, byte elementType)
{
	MultisetElementData elementData;
	elementData.atoms = atoms;
	elementData.multiples = multiples;

	return CreateMultiset(&arrayElementGenerator, &elementData, nUniqueElements, elementType);
}


void AddMultisetToIFactFromArrays(
	IFactDraft * draft,  Atom const atoms[], size32 const multiples[], size32 nUniqueElements, byte elementType)
{
	MultisetElementData elementData;
	elementData.atoms = atoms;
	elementData.multiples = multiples;

	AddMultisetToIFact(draft, &arrayElementGenerator, &elementData, nUniqueElements, elementType);
}


bool IsMultiset(Atom atom)
{
	// NOTE: for now, we assume there are only two multiset relations
	return (
		AtomHasRole(
			atom,
			GetCoreRelation(RELATION_MULTISET_ID),
			GetCoreRoleName(ROLE_MULTISET)) ||
		AtomHasRole(
			atom,
			GetCoreRelation(RELATION_MULTISET_NAME),
			GetCoreRoleName(ROLE_MULTISET)
		)
	);
}

size32 MultisetGetElementMultiple(Atom multiset, Atom element)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, findMultisetElementType(multiset), &iterator);
	size32 multiple = 0;
	while(!multiple && MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		if(SameAtoms(em.element, element))
			multiple = em.multiple;
	}
	MultisetIteratorEnd(&iterator);
	return multiple;
}


/**
 * Multiset iterator
 */
void MultisetIterate(Atom multiset, byte elementType, MultisetIterator * iterator)
{
	Relation const * relation = findMultisetRelation(elementType);
	byte parameterIO[3];
	CoreFormSetByteArray(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(byte[]) {PARAMETER_IN, PARAMETER_OUT, PARAMETER_OUT},
		parameterIO
	);
	Operator const * op = ServiceRegistryFind(
		relation, CreateIOSignature(parameterIO, 3));
	CoreFormSetTuple(
		FORM_MULTISET_ELEMENT_MULTIPLE,
		(Atom[]) {multiset, (Atom) {0}, (Atom) {0}},
		iterator->queryTuple
	);
	iterator->context = OperatorCreateContext(op, iterator->queryTuple);
}


bool MultisetIteratorNext(MultisetIterator * iterator)
{
	return OperatorCall(iterator->context);
}


ElementMultiple MultisetIteratorGetElement(MultisetIterator const * iterator)
{
	Atom multiple = iterator->queryTuple[CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_MULTIPLE)];
	Atom element = iterator->queryTuple[CorePredicateRoleIndex(FORM_MULTISET_ELEMENT_MULTIPLE, ROLE_ELEMENT)];
	return (ElementMultiple) {
		.element = element,
		.multiple = multiple._int
	};
}


void MultisetIteratorEnd(MultisetIterator * iterator)
{
	OperatorFreeContext(iterator->context);
	SetMemory(iterator, sizeof(MultisetIterator), 0);
}


size32 MultisetNUniqueElements(Atom multiset, byte elementType)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, elementType, &iterator);
	size32 nElements = 0;
	while(MultisetIteratorNext(&iterator)) {
		nElements++;
	}
	MultisetIteratorEnd(&iterator);
	return nElements;
}


size32 MultisetSize(Atom multiset, byte elementType)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, elementType, &iterator);
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
	byte elementType = findMultisetElementType(multiset);
	PrintChar('{');
	MultisetIterator iterator;
	MultisetIterate(multiset, elementType, &iterator);
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		PrintTypedAtom(CreateTypedAtom(elementType, em.element));
		PrintF("(%u)", em.multiple);
	}
	MultisetIteratorEnd(&iterator);
	PrintChar('}');
}


void MultisetIterationOrder(Atom multiset, byte elementType, Atom const elements[], index8 order[], size8 nElements)
{
	MultisetIterator iterator;
	MultisetIterate(multiset, elementType, &iterator);
	size8 i = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple em = MultisetIteratorGetElement(&iterator);
		index8 m = 0;
		// find corresponding element in the elements array
		for(index8 j = 0; j < nElements; j++) {
			if(SameAtoms(elements[j], em.element)) {
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

