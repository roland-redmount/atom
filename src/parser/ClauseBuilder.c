
#include "kernel/ifact.h"
#include "kernel/multiset.h"
#include "lang/ClauseForm.h"
#include "lang/Formula.h"
#include "parser/ClauseBuilder.h"
#include "parser/TermBuilder.h"
#include "parser/Tokenizer.h"
#include "util/sort.h"


#define INITIAL_N_TERMS 3


void InitializeClauseBuilder(ClauseBuilder * builder)
{
	InitializeTermBuilder(&(builder->termBuilder));
	CreateResizingArray(&(builder->terms), sizeof(Atom), INITIAL_N_TERMS);
	builder->arity = 0;
	builder->isValid = false;
}


static void addCurrentTerm(ClauseBuilder * builder)
{
	// add current term to array
	Atom term = TermBuilderCreateFormula(&(builder->termBuilder));
	TermBuilderReset(&(builder->termBuilder));
	// update arity
	uint8 termArity = FormulaArity(term);
	ASSERT(builder->arity <= 255 - termArity);
	builder->arity += termArity;
	ResizingArrayAppend(&(builder->terms), &term);
}


bool ClauseBuilderPush(ClauseBuilder * builder, Token token)
{
	if(token.type == TOKEN_OR) {
		if(TermBuilderIsValid(&(builder->termBuilder))) {
			addCurrentTerm(builder);
			builder->isValid = false;
			return true;
		}
		else
			return false;
	}
	else {
		if(TermBuilderPush(&(builder->termBuilder), token)) {
			builder->isValid = TermBuilderIsValid(&(builder->termBuilder));
			return true;
		}
		else
			return false;
	}
}


bool ClauseBuilderIsValid(ClauseBuilder const * builder)
{
	return builder->isValid;
}


static void finishClauseBuilder(ClauseBuilder * builder)
{
	ASSERT(builder->isValid);
	if(!TermBuilderIsEmpty(&(builder->termBuilder))) {
		ASSERT(TermBuilderIsValid(&(builder->termBuilder)));
		addCurrentTerm(builder);
	}
}


Atom ClauseBuilderCreateFormula(ClauseBuilder * builder)
{
	finishClauseBuilder(builder);

	size8 nTerms = ResizingArrayNElements(&(builder->terms));
	Atom const * terms = ResizingArrayGetMemory(&(builder->terms));
	return CreateClause(terms, nTerms);
}


void ClauseBuilderReset(ClauseBuilder * builder)
{
	TermBuilderReset(&(builder->termBuilder));
	size8 nTerms = ResizingArrayNElements(&(builder->terms));
	for(index8 i = 0; i < nTerms; i++) {
		Atom term = *((Atom const *) ResizingArrayGetElement(&(builder->terms), i));
		IFactRelease(term);
	}
	ResizingArrayReset(&(builder->terms));
}


void CleanupClauseBuilder(ClauseBuilder * builder)
{
	ClauseBuilderReset(builder);
	CleanupTermBuilder(&(builder->termBuilder));
	FreeResizingArray(&(builder->terms));
}


Atom CStringToClause(char const * string, size32 length)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	ClauseBuilder builder;
	InitializeClauseBuilder(&builder);
	for(index32 i = 0; i <= length; i++) {
		TokenizerPush(&tokenizer, string[i]);
		if(TokenizerComplete(&tokenizer)) {
			Token token = TokenizerGetToken(&tokenizer);
			ASSERT(ClauseBuilderPush(&builder, token));
			ReleaseToken(token);
			TokenizerReset(&tokenizer);
		}
	}
	ASSERT(ClauseBuilderIsValid(&builder));
	Atom clause = ClauseBuilderCreateFormula(&builder);
	
	CleanupClauseBuilder(&builder);
	TokenizerCleanup(&tokenizer);
	return clause;
}
