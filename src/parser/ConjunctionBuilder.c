
#include "kernel/multiset.h"
#include "lang/formula.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/Tokenizer.h"


#define INITIAL_N_TERMS 3


void InitializeConjunctionBuilder(ConjunctionBuilder * builder, enum FormulaScope scope)
{
	InitializeTermBuilder(&(builder->termBuilder), scope);
	CreateResizingArray(&(builder->terms), sizeof(Atom), INITIAL_N_TERMS);
	builder->arity = 0;
	builder->isEmpty = true;
	builder->isValid = false;
}


/* CLAUDE: True if the conjunction under construction already has the given term. */
static bool conjunctionHasTerm(ConjunctionBuilder const * builder, Atom term)
{
	size8 nTerms = builder->terms.nElements;
	for(index8 i = 0; i < nTerms; i++) {
		if(SameAtoms(term, *((Atom *) ResizingArrayGetElement(&(builder->terms), i))))
			return true;
	}
	return false;
}


/* CLAUDE: Add the current term to the conjunction under construction.
   Returns false if the conjunction already contains that term. */
static bool addCurrentTerm(ConjunctionBuilder * builder)
{
	Atom term = TermBuilderCreateFormula(&(builder->termBuilder));
	if(conjunctionHasTerm(builder, term)) {
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


bool ConjunctionBuilderPush(ConjunctionBuilder * builder, Token token)
{
	// the term builder is offered every token first, including TOKEN_AND;
	// see TermBuilderPush() for why
	if(TermBuilderPush(&(builder->termBuilder), token)) {
		builder->isEmpty = false;
		builder->isValid = TermBuilderIsValid(&(builder->termBuilder));
		return true;
	}

	if((token.type != TOKEN_AND) || !TermBuilderIsValid(&(builder->termBuilder)))
		return false;
	if(!addCurrentTerm(builder))
		return false;
	builder->isValid = false;
	return true;
}


bool ConjunctionBuilderIsEmpty(ConjunctionBuilder const * builder)
{
	return builder->isEmpty;
}


bool ConjunctionBuilderIsValid(ConjunctionBuilder const * builder)
{
	return builder->isValid;
}


bool ConjunctionBuilderIsSingleTerm(ConjunctionBuilder const * builder)
{
	// a term is only appended to the terms array when a TOKEN_AND is accepted
	return builder->terms.nElements == 0;
}


/* CLAUDE: Complete the conjunction builder by adding the current term, if one exists.
   Returns false if that term already exists in the conjunction. */
bool ConjunctionBuilderFinish(ConjunctionBuilder * builder)
{
	ASSERT(builder->isValid);
	if(!TermBuilderIsEmpty(&(builder->termBuilder))) {
		ASSERT(TermBuilderIsValid(&(builder->termBuilder)));
		builder->isValid = addCurrentTerm(builder);
	}
	return builder->isValid;
}


Atom ConjunctionBuilderCreateFormula(ConjunctionBuilder * builder)
{
	ASSERT(builder->isValid);

	size8 nTerms = builder->terms.nElements;
	Atom const * terms = ResizingArrayGetMemory(&(builder->terms));
	return CreateConjunction(terms, nTerms);
}


void ConjunctionBuilderReset(ConjunctionBuilder * builder)
{
	TermBuilderReset(&(builder->termBuilder));
	size8 nTerms = builder->terms.nElements;
	for(index8 i = 0; i < nTerms; i++) {
		Atom term = *((Atom *) ResizingArrayGetElement(&(builder->terms), i));
		ReleaseFormula(term);
	}
	ResizingArrayReset(&(builder->terms));
}


void CleanupConjunctionBuilder(ConjunctionBuilder * builder)
{
	ConjunctionBuilderReset(builder);
	TermBuilderFree(&(builder->termBuilder));
	FreeResizingArray(&(builder->terms));
}


static bool pushToConjunctionBuilder(void * context, Token token)
{
	return ConjunctionBuilderPush((ConjunctionBuilder *) context, token);
}


Atom CStringToConjunction(char const * cString)
{
	ConjunctionBuilder builder;
	InitializeConjunctionBuilder(&builder, FORMULA_TOP_SCOPE);
	TokenizeCString(cString, pushToConjunctionBuilder, &builder);

	ASSERT(ConjunctionBuilderIsValid(&builder))
	bool isFinished = ConjunctionBuilderFinish(&builder);
	ASSERT(isFinished)
	Atom conjunction = ConjunctionBuilderCreateFormula(&builder);
	CleanupConjunctionBuilder(&builder);
	return conjunction;
}
