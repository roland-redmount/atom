/**
 * PredicateBuilder accepts a stream of role, actor tokens
 * where roles may be repeated at any time, and allows generating
 * a predicate formula.
 */

#ifndef PREDICATEBUILDER_H
#define PREDICATEBUILDER_H

#include "lang/formula.h"
#include "parser/PartBuilder.h"
#include "util/ResizingArray.h"


typedef struct s_PredicateBuilder {
	bool isValid;
	PartBuilder partBuilder;
	ResizingArray roles;		// array of AT_NAME atom
	ResizingArray actors;		// array of atoms
} PredicateBuilder;

void InitializePredicateBuilder(PredicateBuilder * builder, enum FormulaScope scope);

bool PredicateBuilderPush(PredicateBuilder * builder, Token token);

bool PredicateBuilderIsEmpty(PredicateBuilder const * builder);

bool PredicateBuilderIsValid(PredicateBuilder const * builder);

/**
 * CLAUDE: Create the predicate formula. Unless roleOrder is 0, the order in which the
 * roles were pushed is written to roleOrder, which must have room for one element per
 * actor; see FormOrdering.
 */
Atom PredicateBuilderCreateFormula(PredicateBuilder const * builder, index8 roleOrder[]);

void PredicateBuilderReset(PredicateBuilder * builder);

void CleanupPredicateBuilder(PredicateBuilder * builder);

Atom CStringToPredicate(char const * string);


#endif	// PREDICATEBUILDER_H
