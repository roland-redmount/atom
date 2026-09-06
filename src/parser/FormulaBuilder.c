
#include "kernel/ifact.h"
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


bool FormulaBuilderFinish(FormulaBuilder * builder)
{
	ConjunctionBuilder * conjunctionBuilder = &(builder->conjunctionBuilder);
	if(!ConjunctionBuilderIsSingleClause(conjunctionBuilder))
		return ConjunctionBuilderFinish(conjunctionBuilder);

	ClauseBuilder * clauseBuilder = &(conjunctionBuilder->clauseBuilder);
	if(!ClauseBuilderIsSingleTerm(clauseBuilder))
		return ClauseBuilderFinish(clauseBuilder);

	// a single term is held by the term builder, and has nothing to finish
	return true;
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


/*
 * Offer the token the tokenizer holds to the builder, and reset the tokenizer so that it
 * can read the next one. Returns false if the builder rejects the token.
 */
static bool pushTokenToBuilder(Tokenizer * tokenizer, FormulaBuilder * builder)
{
	Token token = TokenizerGetToken(tokenizer);
	bool isAccepted = FormulaBuilderPush(builder, token);
	ReleaseToken(token);
	TokenizerReset(tokenizer);
	return isAccepted;
}


/*
 * Push every character of the string to a tokenizer, offering each completed token to the
 * builder. Returns true once the whole string has been read. At the first character the
 * tokenizer rejects, or the first token the builder rejects, this returns false and writes
 * the index where the offending syntax begins to errorPosition. An error within a token is
 * reported at the first character of that token, rather than where the builder noticed it.
 *
 * This is TokenizeCString() with each of its ASSERTs replaced by an error, since a string
 * typed by a user may be invalid syntax; see Tokenizer.h.
 */
static bool tokenizeToFormulaBuilder(
	char const * cString, FormulaBuilder * builder, index32 * errorPosition)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);
	size32 length = CStringLength(cString);
	index32 tokenPosition = 0;
	bool isAccepted = true;

	// NOTE: including the 0 terminator, which completes the last token
	for(index32 i = 0; i <= length; i++) {
		// a tokenizer that has not begun a token starts one at the character pushed next
		if(!tokenizer.type)
			tokenPosition = i;

		enum TokenizerResult result = TokenizerPush(&tokenizer, cString[i]);
		if(result == TOKENIZER_REJECTED) {
			// the character belongs to no token the tokenizer can read
			*errorPosition = i;
			isAccepted = false;
			break;
		}
		if(result == TOKENIZER_ENDED) {
			// the character ended the token before it without being part of it,
			// and has to be pushed again once that token has been taken
			if(!pushTokenToBuilder(&tokenizer, builder)) {
				*errorPosition = tokenPosition;
				isAccepted = false;
				break;
			}
			tokenPosition = i;
			if(TokenizerPush(&tokenizer, cString[i]) != TOKENIZER_ACCEPTED) {
				*errorPosition = i;
				isAccepted = false;
				break;
			}
		}
		if(TokenizerIsFull(&tokenizer) && !pushTokenToBuilder(&tokenizer, builder)) {
			*errorPosition = tokenPosition;
			isAccepted = false;
			break;
		}
	}
	TokenizerFree(&tokenizer);
	return isAccepted;
}


Atom ParseFormula(char const * cString, index32 * errorPosition)
{
	FormulaBuilder builder;
	InitializeFormulaBuilder(&builder);

	Atom formula = (Atom) {0};
	if(tokenizeToFormulaBuilder(cString, &builder, errorPosition)) {
		if(FormulaBuilderIsValid(&builder) && FormulaBuilderFinish(&builder))
			formula = FormulaBuilderCreateFormula(&builder);
		else
			// Either every token was accepted but they do not add up to a formula, or
			// the last term or clause repeats one before it, which is only known once
			// no more tokens follow. Either way the string ends where the missing
			// syntax should have been.
			*errorPosition = CStringLength(cString);
	}
	// A builder abandoned part way through releases whatever it had collected,
	// so this is also the error path; see PartBuilderReset().
	CleanupFormulaBuilder(&builder);
	return formula;
}


Atom CStringToFormula(char const * cString)
{
	index32 errorPosition;
	Atom formula = ParseFormula(cString, &errorPosition);
	ASSERT(formula.hash)
	return formula;
}
