
#ifndef UNIFICATION_H
#define UNIFICATION_H

#include "lang/SubstitutionList.h"

/**
 * Find a unifying Substitution (unifier) for two tuples of the same length, such
 * that applying the Substitution to each tuple yields the same tuple.
 * If the tuples do not unify, returns false.
 */
bool UnifyTuples(TypedTuple const * tuple1, TypedTuple const * tuple2, Substitution * subst);


#endif	// UNIFICATION_H
