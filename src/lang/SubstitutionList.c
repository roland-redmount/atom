
#include "kernel/list.h"
#include "lang/SubstitutionList.h"
#include "memory/allocator.h"


void SetupSubstitutionList(Tuple const * tuple, SubstitutionList * subst)
{
	// over-allocate the variable and atom lists
	// to avoid parsing the tuple twice
	subst->variables = Allocate(sizeof(TypedAtom) * tuple->nAtoms);
	subst->values = Allocate(sizeof(TypedAtom) * tuple->nAtoms);

	// count number of unique variables = no. pairs
	subst->nPairs = 0;
	for(index8 i = 0; i < tuple->nAtoms; i++) {
		TypedAtom a = TupleGetElement(tuple, i);
		if(a.type == AT_VARIABLE)
		{
			// check if variable was already found
			bool newVariable = true;
			for(index8 j = 0; j < subst->nPairs; j++) {
				if(SameTypedAtoms(subst->variables[j], a)) {
					newVariable = false;
					break;
				}
			}
			if(newVariable) {
				// add self-substitution a -> a to list
				subst->variables[subst->nPairs] = a;
				subst->values[subst->nPairs] = a;
				subst->nPairs++;
			}
		}
	}
}


TypedAtom FindSubstValue(SubstitutionList const * subst, TypedAtom variable)
{
	for(index8 i = 0; i < subst->nPairs; i++) {
		if(SameTypedAtoms(subst->variables[i], variable))
			return subst->values[i];
	}
	// variable not found
	return invalidAtom;	
}


void SetSubstValue(SubstitutionList * subst, TypedAtom variable, TypedAtom value)
{
	for(index8 i = 0; i < subst->nPairs; i++) {
		if(SameTypedAtoms(subst->variables[i], variable)) {
			// variable found, change value
			subst->values[i] = value;
			return;
 		}
	}
}


void SubstituteTuple(SubstitutionList const * subst, Tuple const * source, Tuple * destination)
{
	ASSERT(source->nAtoms == destination->nAtoms)
	for(index8 i = 0; i < source->nAtoms; i++) {
		TypedAtom sourceValue = TupleGetElement(source, i);
		TypedAtom substValue = FindSubstValue(subst, sourceValue);
		if(substValue.atom)
			TupleSetElement(destination, i, substValue);
		else
			TupleSetElement(destination, i, sourceValue);
	}
}


void FreeSubstitutionList(SubstitutionList * subst)
{
	// free atom arrays
	Free(subst->variables);
	Free(subst->values);
}


void PrintSubstitutionList(SubstitutionList * subst)
{
	PrintChar('{');
	for(index8 i = 0; i < subst->nPairs; i++) {
		PrintTypedAtom(subst->variables[i]);
		PrintCString(" -> ");
		PrintTypedAtom(subst->values[i]);
		PrintChar(' ');
	}
	PrintChar('}');
}
