/**
 * A substitution list is a list of (variable -> atom) pairs
 * used for variable substitution, similar to a python dict.
 */

#ifndef SUBSTITUTIONLIST_H
#define SUBSTITUTIONLIST_H

#include "lang/TypedAtom.h"


typedef struct s_SubstitutionList {
	uint8 nPairs;	// number of substitutions = no. unique variables
	TypedAtom * variables;
	TypedAtom * values;
} SubstitutionList;


/**
 * Create a substitution list from the unique variables of a tuple,
 * such that each variable x maps to itself (x -> x)
 */
void SetupSubstitutionList(Tuple const * tuple, SubstitutionList * subst);

// Find the value corresponding to a given variable
TypedAtom FindSubstValue(SubstitutionList const * subst, TypedAtom variable);

// Replace a substitution value for a given variable (if it exists)
void SetSubstValue(SubstitutionList * subst, TypedAtom variable, TypedAtom value); 

void FreeSubstitutionList(SubstitutionList * subst);

#endif	// SUBSTITUTIONLIST_H
