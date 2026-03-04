
#ifndef UNIFICATION_H
#define UNIFICATION_H

#include "lang/SubstitutionList.h"

// find substitution list so that two tuples unify
bool UnifyTuples(Atom list1, Atom list2, SubstitutionList* subst1, SubstitutionList* subst2);


#endif	// UNIFICATION_H
