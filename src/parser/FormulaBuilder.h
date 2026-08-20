
#ifndef FORMULABUILDER_H
#define FORMULABUILDER_H


#include "parser/ConjunctionBuilder.h"


/**
 * A FormulaBuilder accepts the tokens of any formula, and yields the simplest
 * formula that holds them: a term if the tokens contain no TOKEN_OR and no
 * TOKEN_AND, a clause if they contain a TOKEN_OR but no TOKEN_AND, and a
 * conjunction otherwise. This is what a reflection [ ... ] parses to, since the
 * kind of formula inside the brackets is not known until the tokens have been read.
 */
typedef struct s_FormulaBuilder {
	ConjunctionBuilder conjunctionBuilder;
} FormulaBuilder;


void InitializeFormulaBuilder(FormulaBuilder * builder);

bool FormulaBuilderPush(FormulaBuilder * builder, Token token);

/**
 * If true, FormulaBuilderCreateFormula() will yield a valid formula.
 */
bool FormulaBuilderIsValid(FormulaBuilder const * builder);

Atom FormulaBuilderCreateFormula(FormulaBuilder * builder);

void FormulaBuilderReset(FormulaBuilder * builder);

void CleanupFormulaBuilder(FormulaBuilder * builder);

/**
 * Parse a C string to a term, clause or conjunction, whichever it turns out to be.
 */
Atom CStringToFormula(char const * cString);


#endif	// FORMULABUILDER_H
