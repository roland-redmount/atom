
#include "kernel/ifact.h"
#include "lang/Formula.h"
#include "lang/TermForm.h"
#include "parser/PredicateBuilder.h"
#include "parser/TermBuilder.h"
#include "parser/Tokenizer.h"


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


Atom CStringToTerm(char const * cString)
{
	size32 length = CStringLength(cString);
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	TermBuilder builder;
	InitializeTermBuilder(&builder);
	for(index32 i = 0; i <= length; i++) {
		TokenizerPush(&tokenizer, cString[i]);
		if(TokenizerComplete(&tokenizer)) {
			Token token = TokenizerGetToken(&tokenizer);
			ASSERT(TermBuilderPush(&builder, token));
			ReleaseToken(token);
			TokenizerReset(&tokenizer);
		}
	}
	ASSERT(TermBuilderIsValid(&builder));
	Atom term = TermBuilderCreateFormula(&builder);
	
	CleanupTermBuilder(&builder);
	TokenizerCleanup(&tokenizer);
	return term;
}
