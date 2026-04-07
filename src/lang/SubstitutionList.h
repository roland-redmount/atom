
#ifndef SUBSTITUTIONLIST_H
#define SUBSTITUTIONLIST_H

#include "lang/TypedAtom.h"

/**
 * A list of (variable -> value) pairs used for variable substitution
 */

typedef struct s_SubstitutionList {
	uint8 nPairs;
	TypedAtom * variables;
	TypedAtom * values;
} SubstitutionList;



// create an array of the unique variables in tuple
SubstitutionList CreateSubstFromVars(Datum list);

// Find the value corresponding to a given variable
TypedAtom FindSubstValue(SubstitutionList subst, TypedAtom variable);
// Replace a substitution value for a given variable (if it exists)
void SetSubstValue(SubstitutionList subst, TypedAtom variable, TypedAtom value); 

void FreeSubstitutionList(SubstitutionList subst);

#endif	// SUBSTITUTIONLIST_H
