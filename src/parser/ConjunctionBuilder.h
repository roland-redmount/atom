
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
Atom ConjunctionBuilderCreateFormula(ConjunctionBuilder * builder);
void ConjunctionBuilderReset(ConjunctionBuilder * builder);

void CleanupConjunctionBuilder(ConjunctionBuilder * builder);

// convenience method for testing
Atom CStringToConjunction(char const * cString);


#endif	// CONJUNCTION_BUILDER_H
