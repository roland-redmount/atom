
#include "kernel/ifact.h"
#include "kernel/string.h"
#include "parser/ClauseBuilder.h"
#include "parser/ConjunctionBuilder.h"
#include "parser/FormulaBuilder.h"
#include "parser/TermBuilder.h"
#include "parser/Tokenizer.h"


void InitializeFormulaBuilder(FormulaBuilder * builder)
{
	InitializeConjunctionBuilder(&(builder->conjunctionBuilder));
}


bool FormulaBuilderPush(FormulaBuilder * builder, Token token)
{
	return ConjunctionBuilderPush(&(builder->conjunctionBuilder), token);
}


bool FormulaBuilderIsValid(FormulaBuilder const * builder)
{
	return ConjunctionBuilderIsValid(&(builder->conjunctionBuilder));
}


/*
 * A conjunction of one clause, or a clause of one term, says no more than the
 * term itself. Each builder still holds the formulas it has collected, so the
 * simplest formula is created by the innermost builder that holds more than one
 * of them. The builders above it never create a formula, and so never flatten
 * what they hold into a conjunction or clause.
 */
Atom FormulaBuilderCreateFormula(FormulaBuilder * builder)
{
	ASSERT(FormulaBuilderIsValid(builder))
	ConjunctionBuilder * conjunctionBuilder = &(builder->conjunctionBuilder);
	if(!ConjunctionBuilderIsSingleClause(conjunctionBuilder))
		return ConjunctionBuilderCreateFormula(conjunctionBuilder);

	ClauseBuilder * clauseBuilder = &(conjunctionBuilder->clauseBuilder);
	if(!ClauseBuilderIsSingleTerm(clauseBuilder))
		return ClauseBuilderCreateFormula(clauseBuilder);

	return TermBuilderCreateFormula(&(clauseBuilder->termBuilder));
}


void FormulaBuilderReset(FormulaBuilder * builder)
{
	ConjunctionBuilderReset(&(builder->conjunctionBuilder));
}


void CleanupFormulaBuilder(FormulaBuilder * builder)
{
	CleanupConjunctionBuilder(&(builder->conjunctionBuilder));
}


Atom CStringToFormula(char const * cString)
{
	size32 length = CStringLength(cString);
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer);
	FormulaBuilder builder;
	InitializeFormulaBuilder(&builder);
	// NOTE: including the 0 terminator
	for(index32 i = 0; i <= length; i++) {
		ASSERT(TokenizerPush(&tokenizer, cString[i]));
		if(TokenizerComplete(&tokenizer)) {
			Token token = TokenizerGetToken(&tokenizer);
			ASSERT(FormulaBuilderPush(&builder, token));
			ReleaseToken(token);
			TokenizerReset(&tokenizer);
		}
	}
	ASSERT(FormulaBuilderIsValid(&builder));
	Atom formula = FormulaBuilderCreateFormula(&builder);

	CleanupFormulaBuilder(&builder);
	TokenizerCleanup(&tokenizer);
	return formula;
}
