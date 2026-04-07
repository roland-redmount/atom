
#include "kernel/ifact.h"
#include "lang/Formula.h"
#include "lang/TermForm.h"
#include "parser/PredicateBuilder.h"
#include "parser/TermBuilder.h"


void InitializeTermBuilder(TermBuilder * builder)
{
	InitializePredicateBuilder(&(builder->predicateBuilder));
	builder->isEmpty = true;
	builder->isValid = false;
	// isNegated is unknown
}


bool TermBuilderPush(TermBuilder * builder, Token token)
{
	if(token.type == TOKEN_NOT) {
		if(builder->isEmpty) {
			builder->sign = false;
			builder->isEmpty = false;
			return true;
		}
		else
			return false;
	}
	else {
		if(PredicateBuilderPush(&(builder->predicateBuilder), token)) {
			if(builder->isEmpty) {
				builder->sign = true;
				builder->isEmpty = false;
			}
			builder->isValid = PredicateBuilderIsValid(&(builder->predicateBuilder));
			return true;
		}
		else
			return false;
	}
}


bool TermBuilderIsValid(TermBuilder const * builder)
{
	return builder->isValid;
}


bool TermBuilderIsEmpty(TermBuilder const * builder)
{
	return builder->isEmpty;
}


Atom TermBuilderCreateFormula(TermBuilder const * builder)
{
	ASSERT(builder->isValid);
	Atom predicate = PredicateBuilderCreateFormula(&(builder->predicateBuilder));
	Atom term = CreateTerm(predicate, builder->sign);
	IFactRelease(predicate);
	return term;
}


void TermBuilderReset(TermBuilder * builder)
{
	PredicateBuilderReset(&(builder->predicateBuilder));
	builder->isEmpty = true;
	builder->isValid = false;
}


void CleanupTermBuilder(TermBuilder * builder)
{
	CleanupPredicateBuilder(&(builder->predicateBuilder));
}
