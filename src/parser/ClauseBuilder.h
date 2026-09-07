
#ifndef CLAUSEBUILDER_H
#define CLAUSEBUILDER_H


#include "lang/Atom.h"
#include "parser/TermBuilder.h"
#include "util/ResizingArray.h"


typedef struct s_ClauseBuilder {
	TermBuilder termBuilder;
	ResizingArray terms;			// array of AT_ID atoms
	size8 arity;
	bool isEmpty;
	bool isValid;
} ClauseBuilder;

void InitializeClauseBuilder(ClauseBuilder * builder, enum FormulaScope scope);

bool ClauseBuilderPush(ClauseBuilder * builder, Token token);

/**
 * Returns true if no tokens have been accepted by the builder.
 */
bool ClauseBuilderIsEmpty(ClauseBuilder const * builder);

/**
 * If true, ClauseBuilderCreateFormula() will yield a valid formula.
 */
bool ClauseBuilderIsValid(ClauseBuilder const * builder);

/**
 * True if the builder has accepted no TOKEN_OR, so that its formula
 * is a clause of a single term. That term is held by the term builder.
 */
bool ClauseBuilderIsSingleTerm(ClauseBuilder const * builder);

/**
 * Complete the clause by adding the current term of the builder.
 * This must be called before ClauseBuilderCreateFormula().
 * Returns false if the completed clause is not valid; see ClauseBuilderIsValid().
 */
bool ClauseBuilderFinish(ClauseBuilder * builder);

/**
 * Create the clause the builder has collected. ClauseBuilderFinish() must have
 * been called, and the builder must be valid.
 */
Atom ClauseBuilderCreateFormula(ClauseBuilder * builder);

void ClauseBuilderReset(ClauseBuilder * builder);

void CleanupClauseBuilder(ClauseBuilder * builder);

/**
 * Parse a C string to a clause
 */
Atom CStringToClause(char const * cString);


#endif	// CLAUSEBUILDER_H
