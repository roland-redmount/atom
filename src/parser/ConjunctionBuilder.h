
#ifndef CONJUNCTION_BUILDER_H
#define CONJUNCTION_BUILDER_H


#include "lang/Atom.h"
#include "parser/ClauseBuilder.h"
#include "util/ResizingArray.h"


typedef struct s_ConjunctionBuilder {
	ClauseBuilder clauseBuilder;
	ResizingArray clauses;			// array of AT_ID atoms
	size8 arity;
	bool isValid;
} ConjunctionBuilder;

void InitializeConjunctionBuilder(ConjunctionBuilder * builder);

bool ConjunctionBuilderPush(ConjunctionBuilder * builder, Token token);

bool ConjunctionBuilderIsValid(ConjunctionBuilder const * builder);

/**
 * True if the builder has accepted no TOKEN_AND, so that its formula
 * is a conjunction of a single clause. That clause is held by the clause builder.
 */
bool ConjunctionBuilderIsSingleClause(ConjunctionBuilder const * builder);

Atom ConjunctionBuilderCreateFormula(ConjunctionBuilder * builder);

void ConjunctionBuilderReset(ConjunctionBuilder * builder);

void CleanupConjunctionBuilder(ConjunctionBuilder * builder);

// convenience method for testing
Atom CStringToConjunction(char const * cString);


#endif	// CONJUNCTION_BUILDER_H
