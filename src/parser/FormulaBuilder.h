
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
 * Create the formula. The order in which the terms and roles were pushed
 * is written to ordering, unless 0.
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
 * without requiring the string to be valid syntax. A string that cannot be parsed into
 * a formula yields the zero atom, and the index into cString where parsing failed is
 * written to errorPosition. If cString ended before the formula was complete,
 * errorPosition is set to the length of cString.
 */
Atom ParseFormula(char const * cString, FormOrdering * ordering, index32 * errorPosition);

// Results of ParseFormulaInput()
#define PARSE_COMPLETE		1
#define PARSE_INCOMPLETE	2
#define PARSE_INVALID		3

/**
 * Parse a C string as ParseFormula() does, and tell an incomplete string from an
 * invalid string. A string is incomplete if every token is accepted, but the string ends
 * before the formula does, as in "foo x <-". More text may complete an incomplete string.
 * If the formula is complete, returns PARSE_COMPLETE and writes the formula to formula;
 * else returns PARSE_INCOMPLETE or PARSE_INVALID and writes the zero atom to formula
 * and the error position to errorPosition.
 */
int ParseFormulaInput(char const * cString, FormOrdering * ordering, Atom * formula, index32 * errorPosition);


#endif	// FORMULABUILDER_H
