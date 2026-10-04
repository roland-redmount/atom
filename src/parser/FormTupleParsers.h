/**
 * Parsers for a term form and for tuple of actors.
 */

#ifndef FORMTUPLEPARSERS_H
#define FORMTUPLEPARSERS_H

#include "kernel/Relation.h"
#include "lang/TypedAtom.h"


/**
 * Parse a C string of role names, such as "parent child", to a term form.
 * A leading ! yields a negated term form, as in "! parent child".
 * A role name may be repeated, as in "+ + =". At most RELATION_MAX_ARITY role
 * names are allowed. The caller obtains a reference to the term form.
 *
 * The role names may be listed in any order. The canonical order of role names
 * in the resulting for is written to roleOrder (at most RELATION_MAX_ARITY elements)
 * so that role at i of the form is number roleOrder[i] in the input string (0-based).
 *
 * A string that is not a term form yields the zero atom, and the index of the
 * character where parsing failed is written to errorIndex; see ParseFormula().
 */
Atom ParseTermForm(char const * cString, index8 roleOrder[], index32 * errorIndex);

/**
 * Parse a C string of actors separated by whitespace, such as "\"Finwe\" 42 'A".
 * An actor is a string, a number, a letter or an ID atom written by its hash.
 * At most maxActors actors are read into the actors array, in the order written,
 * and their number is written to nActors. The caller obtains a reference to each actor.
 *
 * Returns false for a string that is not such a list of actors, and writes the index
 * of the character where parsing failed to errorIndex. No actor is kept in that case.
 */
bool ParseActors(
	char const * cString, TypedAtom actors[], size8 maxActors, size8 * nActors, index32 * errorIndex);


#endif	// FORMTUPLEPARSERS_H
