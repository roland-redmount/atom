/**
 * A predicate form is a multiset of roles
 */ 

#ifndef PREDICATEFORM_H
#define PREDICATEFORM_H

#include "lang/TypedAtom.h"

/**
 * Create a predicate form from a list of roles (DT_NAME),
 * possibly containing duplicates.
 */
Datum CreatePredicateForm(Datum const * roles, size8 nRoles);

bool IsPredicateForm(Datum form);

/**
 * Number of distinct roles, without multiplicity
 */
size8 PredicateNRoles(Datum predicateForm);

/**
 * Arity is the number of actors = number of roles * multiplicity
 */
size8 PredicateArity(Datum predicateForm);

/**
 * 0-based index of the first occurence of the given role (a DT_NAME)
 * The role must exist in predicateForm, or an ASSERT occurs.
 */
index8 PredicateRoleIndex(Datum predicateForm, Datum role);

void PrintPredicateForm(Datum predicateForm);


#endif	// PREDICATEFORM_H
