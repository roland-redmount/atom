
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


/**
 * Return true if the clause under construction already has the given term.
 */
static bool clauseHasTerm(ClauseBuilder const * builder, Atom term)
{
	size8 nTerms = ResizingArrayNElements(&(builder->terms));
	for(index8 i = 0; i < nTerms; i++) {
		if(SameAtoms(term, *((Atom *) ResizingArrayGetElement(&(builder->terms), i))))
			return true;
	}
	return false;
}


/**
 * Add the current term to the clause under construction.
 * Returns false if the clause already contains that rerm.
 */
static bool addCurrentTerm(ClauseBuilder * builder)
{
	// add current term to array
	Atom term = TermBuilderCreateFormula(&(builder->termBuilder));
	if(clauseHasTerm(builder, term)) {
		ReleaseFormula(term);
		return false;
	}
	TermBuilderReset(&(builder->termBuilder));
	// update arity
	uint8 termArity = FormulaArity(term);
	ASSERT(builder->arity <= 255 - termArity);
	builder->arity += termArity;
	ResizingArrayAppend(&(builder->terms), &term);
	return true;
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
	if(!addCurrentTerm(builder))
		return false;
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


/**
 * Complete the clause builder by adding the current term, if one exists.
 * Returns false if the current term already exists in the clause.
 */
bool ClauseBuilderFinish(ClauseBuilder * builder)
{
	ASSERT(builder->isValid);
	if(!TermBuilderIsEmpty(&(builder->termBuilder))) {
		ASSERT(TermBuilderIsValid(&(builder->termBuilder)));
		builder->isValid = addCurrentTerm(builder);
	}
	return builder->isValid;
}


Atom ClauseBuilderCreateFormula(ClauseBuilder * builder)
{
	ASSERT(builder->isValid);

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
	bool isFinished = ClauseBuilderFinish(&builder);
	ASSERT(isFinished)
	Atom clause = ClauseBuilderCreateFormula(&builder);
	CleanupClauseBuilder(&builder);
	return clause;
}
