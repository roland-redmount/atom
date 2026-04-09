
#ifndef CLAUSEBUILDER_H
#define CLAUSEBUILDER_H


#include "lang/Atom.h"
#include "parser/TermBuilder.h"
#include "util/ResizingArray.h"


typedef struct s_ClauseBuilder {
	TermBuilder termBuilder;
	ResizingArray terms;			// array of AT_ID atoms
	size8 arity;
	bool isValid;
} ClauseBuilder;

void InitializeClauseBuilder(ClauseBuilder * builder);

bool ClauseBuilderPush(ClauseBuilder * builder, Token token);
bool ClauseBuilderIsValid(ClauseBuilder const * builder);
Atom ClauseBuilderCreateFormula(ClauseBuilder * builder);
void ClauseBuilderReset(ClauseBuilder * builder);

void CleanupClauseBuilder(ClauseBuilder * builder);

// convenience method for testing
Atom CStringToClause(char const * string, size32 length);


#endif	// CLAUSEBUILDER_H
