/**
 * A term multiset is a multiset of term forms (see TermForm.h) stored in a
 * core relation. Both a clause form and a conjunction form are term multisets;
 * they differ only in the relation that holds them, and the separator printed.
 * See ClauseForm.h and ConjunctionForm.h.
 */

#ifndef TERMMULTISET_H
#define TERMMULTISET_H

#include "lang/TypedAtom.h"

/**
 * Create a term multiset from a list of term forms, possibly containing
 * duplicates, stored in the given core relation.
 */
Atom CreateTermMultisetForm(Atom const termForms[], size8 nTermForms, index32 relationIds);

/**
 * True if the form is a term multiset held by the given relation and role.
 */
bool IsTermMultisetForm(Atom form, index32 relation, index32 roleName);

/**
 * Number of unique term forms, discounting multiplicities.
 */
size8 TermMultisetNUniqueTermForms(Atom form);

/**
 * Total number of terms, including multiplicities.
 */
size8 TermMultisetNTerms(Atom form);

/**
 * Total number of actors, including multiplicities.
 */
size8 TermMultisetArity(Atom form);

/**
 * Find the indices into the actors tuple of the first actor in each term,
 * including multiples. The termIndices array must have at least as many elements
 * as the total number of terms + 1; the last element will be set to the total arity.
 */
void TermMultisetGetTermActorsIndices(Atom form, index8 termActorsIndices[]);


#endif	// TERMMULTISET_H
