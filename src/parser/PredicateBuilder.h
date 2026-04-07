/**
 * PredicateBuilder accepts a stream of role, actor tokens
 * where roles may be repeated at any time, and allows generating
 * a predicate (DT_FORMULA).
 */

#ifndef PREDICATEBUILDER_H
#define PREDICATEBUILDER_H


#include "parser/PartBuilder.h"
#include "util/ResizingArray.h"


typedef struct s_PredicateBuilder {
	bool isValid;
	PartBuilder partBuilder;
	ResizingArray roles;		// array of DT_NAME datum
	ResizingArray actors;		// array of atoms
} PredicateBuilder;

void InitializePredicateBuilder(PredicateBuilder * builder);

bool PredicateBuilderPush(PredicateBuilder * builder, Token token);

bool PredicateBuilderIsEmpty(PredicateBuilder const * builder);
bool PredicateBuilderIsValid(PredicateBuilder const * builder);

Datum PredicateBuilderCreateFormula(PredicateBuilder const * builder);

void PredicateBuilderReset(PredicateBuilder * builder);

void CleanupPredicateBuilder(PredicateBuilder * builder);

Datum CStringToPredicate(char const * string);


#endif	// PREDICATEBUILDER_H
