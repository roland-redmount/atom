
#include "kernel/ifact.h"
#include "kernel/multiset.h"
#include "lang/ClauseForm.h"
#include "lang/formula.h"
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
	builder->isEmpty = true;
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
	// the term builder is offered every token first, including TOKEN_OR;
	// see TermBuilderPush() for why
	if(TermBuilderPush(&(builder->termBuilder), token)) {
		builder->isEmpty = false;
		builder->isValid = TermBuilderIsValid(&(builder->termBuilder));
		return true;
	}

	if((token.type != TOKEN_OR) || !TermBuilderIsValid(&(builder->termBuilder)))
		return false;
	addCurrentTerm(builder);
	builder->isValid = false;
	return true;
}


bool ClauseBuilderIsEmpty(ClauseBuilder const * builder)
{
	return builder->isEmpty;
}


bool ClauseBuilderIsValid(ClauseBuilder const * builder)
{
	return builder->isValid;
}


bool ClauseBuilderIsSingleTerm(ClauseBuilder const * builder)
{
	// a term is only appended to the terms array when a TOKEN_OR is accepted
	return ResizingArrayNElements(&(builder->terms)) == 0;
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
		Atom term = *((Atom *) ResizingArrayGetElement(&(builder->terms), i));
		ReleaseFormula(term);
	}
	ResizingArrayReset(&(builder->terms));
}


void CleanupClauseBuilder(ClauseBuilder * builder)
{
	ClauseBuilderReset(builder);
	CleanupTermBuilder(&(builder->termBuilder));
	FreeResizingArray(&(builder->terms));
}


static bool pushToClauseBuilder(void * context, Token token)
{
	return ClauseBuilderPush((ClauseBuilder *) context, token);
}


Atom CStringToClause(char const * cString)
{
	ClauseBuilder builder;
	InitializeClauseBuilder(&builder);
	TokenizeCString(cString, pushToClauseBuilder, &builder);

	ASSERT(ClauseBuilderIsValid(&builder))
	Atom clause = ClauseBuilderCreateFormula(&builder);
	CleanupClauseBuilder(&builder);
	return clause;
}
