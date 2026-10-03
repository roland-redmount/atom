/**
 * A substitution is a list of (variable -> value) atom pairs
 * used for variable substitution, similar to a python dict.
 */

#ifndef SUBSTITUTION_H
#define SUBSTITUTION_H

#include "kernel/typedtuple.h"


typedef struct s_Substitution {
	uint8 nPairs;		// current number of variable-value pairs
	uint8 capacity;
	TypedAtom * keys;
	TypedAtom * values;
} Substitution;


/**
 * Setup an empty substitution with capacity = maximum number of key-value pairs.
 */
void SetupSubstitution(Substitution * subst, size8 capacity);

/**
 * Find the value for a given key in a substitution list.
 * A key must be a variable or a parameter.
 */
TypedAtom SubstitutionFindValue(Substitution const * subst, TypedAtom key);

/**
 * Add the (key -> value) to the substitution, and replace any (x -> key) currently
 * in the substitution with (x -> value). A key must be a variable or a parameter.
 */
void SubstitutionAdd(Substitution * subst, TypedAtom key, TypedAtom value); 

/**
 * Substitute variables in the source tuple according to the given substitution list
 * and write the result to the destination tuple. Variables in the source
 * tuple that do not occur in the substitution will be copied unchanged
 * to the destination. The source and destination tuples must have the same length.
 * Quoted variables in reflected formula will also be substituted, resulting in
 * a new reflected formula atom in the destination tuple. The destination tuple owns
 * that formula, as it owns every element, so freeing the destination releases it.
 * The source and destination may be the same to substitute in-place.
 */
void SubstituteTuple(Substitution const * subst, TypedTuple const * source, TypedTuple * destination);

/**
 * Deallocate a substitution
 */
void FreeSubstitution(Substitution const * subst);

void PrintSubstitution(Substitution * subst);


#endif	// SUBSTITUTION_H
