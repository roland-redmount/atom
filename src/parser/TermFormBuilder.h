/**
 * A TermFormBuilder accepts the tokens of a term form: an optional TOKEN_NOT
 * followed by the role names, as in "! parent child". A role name may be repeated, as in
 * "+ + =". This is what a reflected form [. ... ] holds; see PartBuilder.
 */

#ifndef TERMFORMBUILDER_H
#define TERMFORMBUILDER_H

#include "kernel/Relation.h"
#include "parser/Token.h"


typedef struct s_TermFormBuilder {
	Atom names[RELATION_MAX_ARITY];
	size8 nNames;
	bool sign;		// false if the term form is negated
} TermFormBuilder;


void InitializeTermFormBuilder(TermFormBuilder * builder);

/**
 * Push a token to the builder. Returns false if the token is not TOKEN_NOT or
 * TOKEN_NAME; if TOKEN_NOT is not the first token; or if the builder already holds
 * RELATION_MAX_ARITY role names.
 */
bool TermFormBuilderPush(TermFormBuilder * builder, Token token);

/**
 * Test whether the builder holds at least one role name.
 */
bool TermFormBuilderIsValid(TermFormBuilder const * builder);

/**
 * Create the term form. The builder must be valid. The caller obtains a reference
 * to the term form. Unless roleOrder is 0, the canonical order of the role names is written
 * to roleOrder, so that role i of the form is role name number roleOrder[i] pushed (0-based).
 */
Atom TermFormBuilderCreateTermForm(TermFormBuilder const * builder, index8 roleOrder[]);

/**
 * CLAUDE: Release the role names held by the builder.
 */
void CleanupTermFormBuilder(TermFormBuilder * builder);

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


#endif	// TERMFORMBUILDER_H
