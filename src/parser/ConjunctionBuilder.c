
#include "kernel/ifact.h"
#include "kernel/multiset.h"
#include "lang/formula.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/Tokenizer.h"
#include "util/sort.h"


#define INITIAL_N_TERMS 3


void InitializeConjunctionBuilder(ConjunctionBuilder * builder)
{
	InitializeClauseBuilder(&(builder->clauseBuilder));
	CreateResizingArray(&(builder->clauses), sizeof(Atom), INITIAL_N_TERMS);
	builder->arity = 0;
	builder->isValid = false;
}


/**
 * Return true if the conjunction under construction contains the given clause.
 */
static bool conjunctionHasClause(ConjunctionBuilder const * builder, Atom clause)
{
	size8 nClauses = ResizingArrayNElements(&(builder->clauses));
	for(index8 i = 0; i < nClauses; i++) {
		if(SameAtoms(clause, *((Atom *) ResizingArrayGetElement(&(builder->clauses), i))))
			return true;
	}
	return false;
}


/**
 * Add the builder's current clause to the conjunction under construction.
 * Returns false if that clause is invalid, or if the conjunction already contains the clause.
 */
static bool addCurrentClause(ConjunctionBuilder * builder)
{
	// add current clause to array
	if(!ClauseBuilderFinish(&(builder->clauseBuilder)))
		return false;
	Atom clause = ClauseBuilderCreateFormula(&(builder->clauseBuilder));
	if(conjunctionHasClause(builder, clause)) {
		ReleaseFormula(clause);
		return false;
	}
	ClauseBuilderReset(&(builder->clauseBuilder));
	// update arity
	uint8 clauseArity = FormulaArity(clause);
	ASSERT(builder->arity <= 255 - clauseArity);
	builder->arity += clauseArity;
	ResizingArrayAppend(&(builder->clauses), &clause);
	return true;
}


bool ConjunctionBuilderPush(ConjunctionBuilder * builder, Token token)
{
	// the clause builder is offered every token first, including TOKEN_AND;
	// see TermBuilderPush() for why
	if(ClauseBuilderPush(&(builder->clauseBuilder), token)) {
		builder->isValid = ClauseBuilderIsValid(&(builder->clauseBuilder));
		return true;
	}

	if((token.type != TOKEN_AND) || !ClauseBuilderIsValid(&(builder->clauseBuilder)))
		return false;
	if(!addCurrentClause(builder))
		return false;
	builder->isValid = false;
	return true;
}


bool ConjunctionBuilderIsValid(ConjunctionBuilder const * builder)
{
	return builder->isValid;
}


bool ConjunctionBuilderIsSingleClause(ConjunctionBuilder const * builder)
{
	// A clause is only appended to the clauses array when a TOKEN_AND is accepted,
	// so a count of zero elements means we at most one clause.
	return ResizingArrayNElements(&(builder->clauses)) == 0;
}


/**
 * Complete the conjunction builder by adding the current clause, if one exists.
 * Returns false if that clause cannot be added; see addCurrentClause().
 */
bool ConjunctionBuilderFinish(ConjunctionBuilder * builder)
{
	ASSERT(builder->isValid);
	if(!ClauseBuilderIsEmpty(&(builder->clauseBuilder))) {
		ASSERT(ClauseBuilderIsValid(&(builder->clauseBuilder)));
		builder->isValid = addCurrentClause(builder);
	}
	return builder->isValid;
}


Atom ConjunctionBuilderCreateFormula(ConjunctionBuilder * builder)
{
	ASSERT(builder->isValid);

	size8 nClauses = ResizingArrayNElements(&(builder->clauses));
	Atom const * clauses = ResizingArrayGetMemory(&(builder->clauses));
	return CreateConjunction(clauses, nClauses);
}


void ConjunctionBuilderReset(ConjunctionBuilder * builder)
{
	ClauseBuilderReset(&(builder->clauseBuilder));
	size8 nClauses = ResizingArrayNElements(&(builder->clauses));
	for(index8 i = 0; i < nClauses; i++) {
		Atom clause = *((Atom *) ResizingArrayGetElement(&(builder->clauses), i));
		ReleaseFormula(clause);
	}
	ResizingArrayReset(&(builder->clauses));
}


void CleanupConjunctionBuilder(ConjunctionBuilder * builder)
{
	ConjunctionBuilderReset(builder);
	CleanupClauseBuilder(&(builder->clauseBuilder));
	FreeResizingArray(&(builder->clauses));
}


static bool pushToConjunctionBuilder(void * context, Token token)
{
	return ConjunctionBuilderPush((ConjunctionBuilder *) context, token);
}


Atom CStringToConjunction(char const * cString)
{
	ConjunctionBuilder builder;
	InitializeConjunctionBuilder(&builder);
	TokenizeCString(cString, pushToConjunctionBuilder, &builder);

	ASSERT(ConjunctionBuilderIsValid(&builder))
	bool isFinished = ConjunctionBuilderFinish(&builder);
	ASSERT(isFinished)
	Atom conjunction = ConjunctionBuilderCreateFormula(&builder);
	CleanupConjunctionBuilder(&builder);
	return conjunction;
}
