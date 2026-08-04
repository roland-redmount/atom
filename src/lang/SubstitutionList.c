
#include "kernel/list.h"
#include "lang/SubstitutionList.h"
#include "memory/allocator.h"


void SetupSubstitution(Substitution * subst, size8 capacity)
{
	// Do a single allocation for keys and values arrays
	subst->keys = Allocate(2 * sizeof(TypedAtom) * capacity);
	SetMemory(subst->keys, 2 * sizeof(TypedAtom) * capacity, 0); 
	subst->values = subst->keys + capacity;
	subst->capacity = capacity;
	subst->nPairs = 0;
}


TypedAtom SubstitutionFindValue(Substitution const * subst, TypedAtom key)
{
	for(index8 i = 0; i < subst->nPairs; i++) {
		if(SameTypedAtoms(subst->keys[i], key))
			return subst->values[i];
	}
	// variable not found
	return invalidAtom;	
}


void SubstitutionSetValue(Substitution * subst, TypedAtom key, TypedAtom value)
{
	index8 i = 0;
	while(i < subst->nPairs) {
		if(SameTypedAtoms(subst->keys[i], key)) {
			// key found, change its value
			subst->values[i] = value;
			return;
 		}
		i++;
	}
	// else add new key-value pair
	ASSERT(i < subst->capacity)
	subst->keys[i] = key;
	subst->values[i] = value;
	subst->nPairs++;
}


void SubstituteTuple(Substitution const * subst, TypedTuple const * source, TypedTuple * destination)
{
	ASSERT(source->nAtoms == destination->nAtoms)
	for(index8 i = 0; i < source->nAtoms; i++) {
		TypedAtom sourceValue = TypedTupleGetElement(source, i);
		TypedAtom substValue = SubstitutionFindValue(subst, sourceValue);
		if(substValue.type)
			TypedTupleSetElement(destination, i, substValue);
		else
			TypedTupleSetElement(destination, i, sourceValue);
	}
}


void FreeSubstitution(Substitution * subst)
{
	// free atom arrays
	Free(subst->keys);
}


void PrintSubstitution(Substitution * subst)
{
	PrintChar('{');
	for(index8 i = 0; i < subst->nPairs; i++) {
		PrintTypedAtom(subst->keys[i]);
		PrintCString(" -> ");
		PrintTypedAtom(subst->values[i]);
		PrintChar(' ');
	}
	PrintChar('}');
}
