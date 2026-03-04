
#include <stdlib.h>

#include "kernel/list.h"
#include "lang/SubstitutionList.h"

/**
 * Allocate pointer arrays for a substitution list
 * Pointers are not set!
 * 
 * TODO: can we use just an array of Atoms for this,
 * and avoid using malloc() ?
 */
static SubstitutionList allocateSubstList(uint8 nPairs)
{
	SubstitutionList subst;
	subst.nPairs = nPairs;
	subst.variables = malloc(sizeof(Atom*) * nPairs);
	subst.values = malloc(sizeof(Atom*) * nPairs);
	return subst;
}

/**
 * Create a substitution list from the unique variables of a tuple,
 * such that each variable maps to itself, var -> var
 */
SubstitutionList CreateSubstFromVars(Atom list)
{
	size32 nElements = ListLength(list);
	ASSERT(nElements <= 255);
	PrintF("CreateSubstFromVars() nAtoms = %u\n", nElements);
	// over-allocate the variable and atom lists
	// to avoid parsing the tuple twice
	SubstitutionList subst = allocateSubstList(nElements);

	// count number of unique variables = no. pairs
	subst.nPairs = 0;
	for(index8 i = 0; i < nElements; i++) {
		Atom a = ListGetElement(list, i+1);
		PrintF("atom %u ", i);
		if(a.type == DT_VARIABLE)
		{
			// check if variable was already found
			bool newVariable = true;
			for(index8 j = 0; j < subst.nPairs; j++) {
				if(SameAtoms(subst.variables[i], a)) {
					PrintF("variable exists\n");
					newVariable = false;
					break;
				}
			}
			if(newVariable) {
				PrintF("new variable\n");
				// add to substitution list
				subst.variables[i] = a;
				subst.values[i] = a;
				subst.nPairs++;
			}
		}
	}
	return subst;
}


/**
 * Find the value corresponding to a given variable
 */
Atom FindSubstValue(SubstitutionList subst, Atom variable)
{
	for(index8 i = 0; i < subst.nPairs; i++) {
		if(SameAtoms(subst.variables[i], variable))
			return subst.values[i];
	}
	// variable not found
	return invalidAtom;	
}

/**
 * Replace a substitution value for a given variable (if it exists)
 */
void SetSubstValue(SubstitutionList subst, Atom variable, Atom value)
{
	for(index8 i = 0; i < subst.nPairs; i++) {
		if(SameAtoms(subst.variables[i], variable)) {
			// variable found, change value
			subst.values[i] = value;
			return;
 		}
	}
}

/**
 * Deallocate a substitution list
 */
void FreeSubstitutionList(SubstitutionList subst)
{
	// free atom arrays
	free(subst.variables);
	free(subst.values);
}
