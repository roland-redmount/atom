
#ifndef CONJUNCTION_BUILDER_H
#define CONJUNCTION_BUILDER_H


#include "lang/Atom.h"
#include "parser/TermBuilder.h"
#include "util/ResizingArray.h"


/* CLAUDE: A conjunction builder collects terms joined by TOKEN_AND into a
   conjunction. It mirrors the clause builder, which joins terms with TOKEN_OR;
   see ClauseBuilder.h. */
typedef struct s_ConjunctionBuilder {
	TermBuilder termBuilder;
	ResizingArray terms;			// array of AT_ID atoms
	size8 arity;
	bool isEmpty;
	bool isValid;
} ConjunctionBuilder;

void InitializeConjunctionBuilder(ConjunctionBuilder * builder, enum FormulaScope scope);

bool ConjunctionBuilderPush(ConjunctionBuilder * builder, Token token);

/* CLAUDE: True if no tokens have been accepted by the builder. */
bool ConjunctionBuilderIsEmpty(ConjunctionBuilder const * builder);

bool ConjunctionBuilderIsValid(ConjunctionBuilder const * builder);

/* CLAUDE: True if the builder has accepted no TOKEN_AND, so that its formula
   is a conjunction of a single term. That term is held by the term builder. */
bool ConjunctionBuilderIsSingleTerm(ConjunctionBuilder const * builder);

/**
 * Complete the conjunction by adding the current term of the builder.
 * This must be called before ConjunctionBuilderCreateFormula().
 * Returns false if the completed conjunction is not valid; see ConjunctionBuilderIsValid().
 */
bool ConjunctionBuilderFinish(ConjunctionBuilder * builder);

/**
 * Create the conjunction the builder has collected. ConjunctionBuilderFinish()
 * must have been called, and the builder must be valid.
 */
Atom ConjunctionBuilderCreateFormula(ConjunctionBuilder * builder);

void ConjunctionBuilderReset(ConjunctionBuilder * builder);

void CleanupConjunctionBuilder(ConjunctionBuilder * builder);

// convenience method for testing
Atom CStringToConjunction(char const * cString);


#endif	// CONJUNCTION_BUILDER_H
