
#include "kernel/ifact.h"
#include "lang/formula.h"
#include "lang/TermForm.h"
#include "parser/PredicateBuilder.h"
#include "parser/TermBuilder.h"
#include "parser/Tokenizer.h"


void InitializeTermBuilder(TermBuilder * builder, enum FormulaScope scope)
{
	InitializePredicateBuilder(&(builder->predicateBuilder), scope);
	builder->isEmpty = true;
	builder->isValid = false;
	// isNegated is unknown
}


bool TermBuilderPush(TermBuilder * builder, Token token)
{
	// The predicate builder is offered every token first, including TOKEN_NOT,
	// because a reflection being collected below this term claims the operator
	// tokens written inside it. Only a token the predicate builder rejects can
	// belong to this term.
	if(PredicateBuilderPush(&(builder->predicateBuilder), token)) {
		if(builder->isEmpty) {
			builder->sign = true;
			builder->isEmpty = false;
		}
		builder->isValid = PredicateBuilderIsValid(&(builder->predicateBuilder));
		return true;
	}

	if((token.type != TOKEN_NOT) || !builder->isEmpty)
		return false;
	builder->sign = false;
	builder->isEmpty = false;
	return true;
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
	// CreateTerm() copies the actors, so the intermediate predicate formula
	// is no longer needed here.
	ReleaseFormula(predicate);
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


bool TermBuilderTokenHandler(void * context, Token token)
{
	return TermBuilderPush((TermBuilder *) context, token);
}


Atom CStringToTerm(char const * cString)
{
	TermBuilder builder;
	InitializeTermBuilder(&builder, FORMULA_TOP_SCOPE);
	TokenizeCString(cString, TermBuilderTokenHandler, &builder);

	ASSERT(TermBuilderIsValid(&builder))
	Atom term = TermBuilderCreateFormula(&builder);
	CleanupTermBuilder(&builder);
	return term;
}
