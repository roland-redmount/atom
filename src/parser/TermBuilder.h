
#ifndef TERMBUILDER_H
#define TERMBUILDER_H


#include "parser/PredicateBuilder.h"


typedef struct s_TermBuilder {
	bool isEmpty;
	bool isValid;
	bool sign;
	PredicateBuilder predicateBuilder;
} TermBuilder;


void InitializeTermBuilder(TermBuilder *);

bool TermBuilderPush(TermBuilder * builder, Token token);
bool TermBuilderIsValid(TermBuilder const * builder);
bool TermBuilderIsEmpty(TermBuilder const * builder);

Atom TermBuilderCreateFormula(TermBuilder const * builder);

void TermBuilderReset(TermBuilder * builder);
void CleanupTermBuilder(TermBuilder * builder);


#endif	// TERMBUILDER_H
