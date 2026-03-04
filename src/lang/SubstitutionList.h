
#ifndef SUBSTITUTIONLIST_H
#define SUBSTITUTIONLIST_H

#include "lang/Atom.h"

/**
 * A list of (variable -> value) pairs used for variable substitution
 */

typedef struct s_SubstitutionList {
	uint8 nPairs;
	Atom *variables;
	Atom *values;
} SubstitutionList;



// create an array of the unique variables in tuple
SubstitutionList CreateSubstFromVars(Atom list);

// Find the value corresponding to a given variable
Atom FindSubstValue(SubstitutionList subst, Atom variable);
// Replace a substitution value for a given variable (if it exists)
void SetSubstValue(SubstitutionList subst, Atom variable, Atom value); 

void FreeSubstitutionList(SubstitutionList subst);

#endif	// SUBSTITUTIONLIST_H
