
#ifndef FORMULABUILDER_H
#define FORMULABUILDER_H


#include "lang/Atom.h"
#include "parser/TermBuilder.h"
#include "util/ResizingArray.h"


/**
 * A FormulaBuilder accepts the tokens of any formula, and yields the simplest
 * formula that holds them: a term if the tokens contain no TOKEN_OR and no
 * TOKEN_AND, a clause if they contain TOKEN_OR ('|'), and a conjunction if they
 * contain TOKEN_AND ('&'). A formula cannot include both TOKEN_OR and TOKEN_AND tokens.
 *
 * A clause can also be written with a TOKEN_IMPLIED_BY ("<-") following the first term,
 * for example "foo x <- bar x & baz x", which yields the clause (foo x | ! bar x | ! baz x).
 * See FormulaBuilderPush().
 */
typedef struct s_FormulaBuilder {
	TermBuilder termBuilder;
	ResizingArray terms;			// array of AT_ID atoms
	// CLAUDE: the role order of each term in terms, one after the other; see FormOrdering
	ResizingArray termRoleOrders;
	size8 arity;
	uint8 connective;				// the connective accepted so far; see FormulaBuilder.c
	bool isValid;
} FormulaBuilder;


void InitializeFormulaBuilder(FormulaBuilder * builder, enum FormulaScope scope);

bool FormulaBuilderPush(FormulaBuilder * builder, Token token);

/**
 * If true, FormulaBuilderCreateFormula() will yield a valid formula.
 */
bool FormulaBuilderIsValid(FormulaBuilder const * builder);

/**
 * Finalize the formula builder, adding any remaining term.
 * This must be called before calling FormulaBuilderCreateFormula().
 * Returns false if the formula is not valid.
 */
bool FormulaBuilderFinish(FormulaBuilder * builder);

/**
 * CLAUDE: Create the formula. Unless ordering is 0, the order in which the terms and
 * roles were pushed is written to ordering.
 */
Atom FormulaBuilderCreateFormula(FormulaBuilder * builder, FormOrdering * ordering);

void FormulaBuilderReset(FormulaBuilder * builder);

void CleanupFormulaBuilder(FormulaBuilder * builder);

/**
 * Parse a C string to a term, clause or conjunction, whichever it turns out to be.
 * The string must be a valid formula, or an ASSERT will be triggered; see ParseFormula()
 * for a caller that cannot make that promise.
 */
Atom CStringToFormula(char const * cString);

/**
 * Parse a C string to a term, clause or conjunction, whichever it turns out to be,
 * without requiring the string to be valid syntax. A string that is not a formula is a
 * normal outcome here, unlike with CStringToFormula(): it yields the zero atom, whose
 * hash the caller tests, and the index of the character parsing failed at is written to
 * errorPosition. That index is the length of
 * the string when the string ends in the middle of a formula, which is also what an
 * empty string yields.
 *
 * CLAUDE: Unless ordering is 0, the order in which the terms and roles of the formula
 * were written is written to ordering; see FormOrdering.
 */
Atom ParseFormula(char const * cString, FormOrdering * ordering, index32 * errorPosition);


#endif	// FORMULABUILDER_H
