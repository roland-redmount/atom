
#ifndef UNIFICATION_H
#define UNIFICATION_H

#include "lang/SubstitutionList.h"

/**
 * Find a unifying substitution (unifier) for two tuples of the same length.
 * If a unifier exists, it is always unique. Generates one substitution list
 * for each tuple, such that applying these substitutions results in the same
 * (unified) tuple in both cases.
 * If the tuples do not unify, returns false.
 */
bool UnifyTuples(Tuple const * tuple1, Tuple const * tuple2, SubstitutionList * subst1, SubstitutionList * subst2);


#endif	// UNIFICATION_H
