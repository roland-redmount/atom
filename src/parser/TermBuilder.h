
#ifndef TERMBUILDER_H
#define TERMBUILDER_H


#include "parser/PredicateBuilder.h"


typedef struct s_TermBuilder {
	bool isEmpty;
	bool isValid;
	bool sign;
	PredicateBuilder predicateBuilder;
} TermBuilder;


void InitializeTermBuilder(TermBuilder * builder, enum FormulaScope scope);

bool TermBuilderPush(TermBuilder * builder, Token token);

/**
 * If true, TermBuilderCreateFormula() will yield a valid formula.
 */
bool TermBuilderIsValid(TermBuilder const * builder);

/**
 * Returns true if no tokens have been accepted by the builder.
 */
bool TermBuilderIsEmpty(TermBuilder const * builder);

Atom TermBuilderCreateFormula(TermBuilder const * builder);

void TermBuilderReset(TermBuilder * builder);

void TermBuilderFree(TermBuilder * builder);

/**
 * Parse a C string to a term
 */
Atom CStringToTerm(char const * cString);

/**
 * A TokenHandler function using a TermBuilder, for use with TokenizeCString().
 * The context argument must be a TermBuilder * pointer.
 */
bool TermBuilderTokenHandler(void * context, Token token);


#endif	// TERMBUILDER_H
