
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
				if(SameTypedAtoms(subst->variables[i], a)) {
					newVariable = false;
					break;
				}
			}
			if(newVariable) {
				// add to substitution list
				subst->variables[i] = a;
				subst->values[i] = a;
				subst->nPairs++;
			}
		}
	}
}


/**
 * Find the value corresponding to a given variable
 */
TypedAtom FindSubstValue(SubstitutionList const * subst, TypedAtom variable)
{
	for(index8 i = 0; i < subst->nPairs; i++) {
		if(SameTypedAtoms(subst->variables[i], variable))
			return subst->values[i];
	}
	// variable not found
	return invalidAtom;	
}

/**
 * Replace a substitution value for a given variable (if it exists)
 */
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

/**
 * Deallocate a substitution list
 */
void FreeSubstitutionList(SubstitutionList * subst)
{
	// free atom arrays
	Free(subst->variables);
	Free(subst->values);
}
