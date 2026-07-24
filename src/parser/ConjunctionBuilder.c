
#include "kernel/ifact.h"
#include "kernel/multiset.h"
#include "lang/Formula.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/Tokenizer.h"
#include "util/sort.h"


#define INITIAL_N_TERMS 3


void InitializeConjunctionBuilder(ConjunctionBuilder * builder)
{
	InitializeClauseBuilder(&(builder->clauseBuilder));
	CreateResizingArray(&(builder->clauses), sizeof(Formula *), INITIAL_N_TERMS);
	builder->arity = 0;
	builder->isValid = false;
}


static void addCurrentClause(ConjunctionBuilder * builder)
{
	// add current clause to array
	Formula * clause = ClauseBuilderCreateFormula(&(builder->clauseBuilder));
	ClauseBuilderReset(&(builder->clauseBuilder));
	// update arity
	uint8 clauseArity = FormulaArity(clause);
	ASSERT(builder->arity <= 255 - clauseArity);
	builder->arity += clauseArity;
	ResizingArrayAppend(&(builder->clauses), &clause);
}


bool ConjunctionBuilderPush(ConjunctionBuilder * builder, Token token)
{
	if(token.type == TOKEN_AND) {
		if(ClauseBuilderIsValid(&(builder->clauseBuilder))) {
			addCurrentClause(builder);
			builder->isValid = false;
			return true;
		}
		else
			return false;
	}
	else {
		if(ClauseBuilderPush(&(builder->clauseBuilder), token)) {
			builder->isValid = ClauseBuilderIsValid(&(builder->clauseBuilder));
			return true;
		}
		else
			return false;
	}
}


bool ConjunctionBuilderIsValid(ConjunctionBuilder const * builder)
{
	return builder->isValid;
}


static void finishConjunctionBuilder(ConjunctionBuilder * builder)
{
	ASSERT(builder->isValid);
	if(!ClauseBuilderIsEmpty(&(builder->clauseBuilder))) {
		ASSERT(ClauseBuilderIsValid(&(builder->clauseBuilder)));
		addCurrentClause(builder);
	}
}


Formula * ConjunctionBuilderCreateFormula(ConjunctionBuilder * builder)
{
	finishConjunctionBuilder(builder);

	size8 nClauses = ResizingArrayNElements(&(builder->clauses));
	Formula const ** clauses = ResizingArrayGetMemory(&(builder->clauses));
	return CreateConjunction(clauses, nClauses);
}


void ConjunctionBuilderReset(ConjunctionBuilder * builder)
{
	ClauseBuilderReset(&(builder->clauseBuilder));
	size8 nClauses = ResizingArrayNElements(&(builder->clauses));
	for(index8 i = 0; i < nClauses; i++) {
		Atom clause = *((Atom const *) ResizingArrayGetElement(&(builder->clauses), i));
		IFactRelease(clause);
	}
	ResizingArrayReset(&(builder->clauses));
}


void CleanupConjunctionBuilder(ConjunctionBuilder * builder)
{
	ConjunctionBuilderReset(builder);
	CleanupClauseBuilder(&(builder->clauseBuilder));
	FreeResizingArray(&(builder->clauses));
}


Formula * CStringToConjunction(char const * cString)
{
	size32 length = CStringLength(cString);
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	ConjunctionBuilder builder;
	InitializeConjunctionBuilder(&builder);
	for(index32 i = 0; i <= length; i++) {
		TokenizerPush(&tokenizer, cString[i]);
		if(TokenizerComplete(&tokenizer)) {
			Token token = TokenizerGetToken(&tokenizer);
			ASSERT(ConjunctionBuilderPush(&builder, token));
			ReleaseToken(token);
			TokenizerReset(&tokenizer);
		}
	}
	ASSERT(ConjunctionBuilderIsValid(&builder));
	Formula * conjuction = ConjunctionBuilderCreateFormula(&builder);
	
	CleanupConjunctionBuilder(&builder);
	TokenizerCleanup(&tokenizer);
	return conjuction;
}
