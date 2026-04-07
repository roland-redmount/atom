
#ifndef UNIFICATION_H
#define UNIFICATION_H

#include "lang/SubstitutionList.h"

// find substitution list so that two tuples unify
bool UnifyTuples(Datum list1, Datum list2, SubstitutionList * subst1, SubstitutionList * subst2);


#endif	// UNIFICATION_H
