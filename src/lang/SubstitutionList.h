/**
 * A substitution is a list of (key -> value) atom pairs
 * used for variable substitution, similar to a python dict.
 */

#ifndef SUBSTITUTION_H
#define SUBSTITUTION_H

#include "lang/TypedAtom.h"


typedef struct s_Substitution {
	uint8 nPairs;	// current number of key-value pairs
	uint8 capacity;
	TypedAtom * keys;
	TypedAtom * values;
} Substitution;


/**
 * Setup an empty substitution with capacity = maximum number of key-value pairs.
 */
void SetupSubstitution(Substitution * subst, size8 capacity);

/**
 * Find the value corresponding to a given key
 */
TypedAtom SubstitutionFindValue(Substitution const * subst, TypedAtom key);

/**
 * Replace the value for a key, if it exists
 */
void SubstitutionSetValue(Substitution * subst, TypedAtom key, TypedAtom value); 

/**
 * Substitute values in the source tuple according to the given substitution
 * and write corresponding values to the destination tuple. Atoms in the source
 * tuple that are not keys in the substitution will be copied unchanged
 * to the destination.
 * The source and destination may be the same to substitute in-place.
 */
void SubstituteTuple(Substitution const * subst, Tuple const * source, Tuple * destination);

/**
 * Deallocate a substitution
 */
void FreeSubstitution(Substitution * subst);

void PrintSubstitution(Substitution * subst);


#endif	// SUBSTITUTION_H
