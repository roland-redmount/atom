/**
 * A predicate form is a multiset of roles
 */ 

#ifndef PREDICATEFORM_H
#define PREDICATEFORM_H

#include "lang/TypedAtom.h"

/**
 * Create a predicate form from an array of roles (AT_NAME),
 * possibly containing duplicates.
 */
Atom CreatePredicateForm(Atom const roles[], size8 nRoles);


/**
 * Create a predicate form from an array of roles (AT_NAME),
 * with a custom assertFact function. This is used when bootstrapping the kernel.
 */
Atom CreatePredicateFormBootstrap(Atom const roles[], size8 nRoles, void (* assertFact)(Atom, TypedAtom *));


bool IsPredicateForm(Atom form);

/**
 * Number of distinct roles, without multiplicity
 */
size8 PredicateNRoles(Atom predicateForm);

/**
 * Arity is the number of actors = number of roles * multiplicity
 * TODO: rename this PredicateFormArity() ?
 */
size8 PredicateArity(Atom predicateForm);

/**
 * 0-based index of the first occurence of the given role (a AT_NAME)
 * The role must exist in predicateForm, or an ASSERT occurs.
 * 
 * TODO: it is highly inefficient to call this runtime to find out indices
 * of roles. The ordering of role names in a predicate is determined by
 * the iteration order of (multiset element multiple), which should be
 * dictated solely by ordering of the name strings (lexiographic order).
 * Hence, the name indices is known at compile time, and could be precomputed
 * and stored somewhere. This is analogous to relocating symbols in object files.
 * -> An alternative is to always store predicates role name in the order
 *    specified by the user when first creating the predicate. When comparing
 *    predicate forms for equality, we would have to sort names lexiographic
 *    to determine equality without regard to order, e.g. (+ + = ) == (= + +)
 */
index8 PredicateRoleIndex(Atom predicateForm, Atom roleName);

void PrintPredicateForm(Atom predicateForm);


#endif	// PREDICATEFORM_H
