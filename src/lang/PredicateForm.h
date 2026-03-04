/**
 * A predicate form is a multiset of roles
 */ 

#ifndef PREDICATEFORM_H
#define PREDICATEFORM_H

#include "Atom.h"

/**
 * Create a predicate form from a list of roles (DT_NAME),
 * possibly containing duplicates.
 */
Atom CreatePredicateForm(Atom const * roles, size8 nRoles);

bool IsPredicateForm(Atom form);

/**
 * Number of distinct roles, without multiplicity
 */
size8 PredicateNRoles(Atom predicateForm);

/**
 * Arity is the number of actors = number of roles * multiplicity
 */
size8 PredicateArity(Atom predicateForm);

/**
 * 0-based index of the first occurence of the given role (a DT_NAME)
 * The role must exist in predicateForm, or an ASSERT occurs.
 */
index8 PredicateRoleIndex(Atom predicateForm, Atom role);

void PrintPredicateForm(Atom predicateForm);


#endif	// PREDICATEFORM_H
