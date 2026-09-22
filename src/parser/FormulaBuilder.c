
#include "lang/formula.h"
#include "parser/FormulaBuilder.h"
#include "parser/Tokenizer.h"


#define INITIAL_N_TERMS 3

// the connective a formula builder has accepted so far
#define CONNECTIVE_NONE		0		// no TOKEN_OR or TOKEN_AND yet
#define CONNECTIVE_OR		1		// a disjunction, yielding a clause
#define CONNECTIVE_AND		2		// a conjunction, yielding a conjunction


void InitializeFormulaBuilder(FormulaBuilder * builder, enum FormulaScope scope)
{
	InitializeTermBuilder(&(builder->termBuilder), scope);
	CreateResizingArray(&(builder->terms), sizeof(Atom), INITIAL_N_TERMS);
	builder->arity = 0;
	builder->connective = CONNECTIVE_NONE;
	builder->isValid = false;
}


/* CLAUDE: True if the formula under construction already has the given term. */
static bool formulaHasTerm(FormulaBuilder const * builder, Atom term)
{
	size8 nTerms = builder->terms.nElements;
	for(index8 i = 0; i < nTerms; i++) {
		if(SameAtoms(term, *((Atom *) ResizingArrayGetElement(&(builder->terms), i))))
			return true;
	}
	return false;
}


/* CLAUDE: Add the current term to the formula under construction.
   Returns false if the formula already contains that term. */
static bool addCurrentTerm(FormulaBuilder * builder)
{
	Atom term = TermBuilderCreateFormula(&(builder->termBuilder));
	if(formulaHasTerm(builder, term)) {
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


bool FormulaBuilderPush(FormulaBuilder * builder, Token token)
{
	// the term builder is offered every token first, including a connective;
	// see TermBuilderPush() for why
	if(TermBuilderPush(&(builder->termBuilder), token)) {
		builder->isValid = TermBuilderIsValid(&(builder->termBuilder));
		return true;
	}

	// only a connective joins the current term to the next
	uint8 tokenConnective;
	if(token.type == TOKEN_OR)
		tokenConnective = CONNECTIVE_OR;
	else if(token.type == TOKEN_AND)
		tokenConnective = CONNECTIVE_AND;
	else
		return false;

	// the first connective fixes the kind of formula. A different connective
	// later would mix a disjunction and a conjunction, which is not a formula.
	if(builder->connective == CONNECTIVE_NONE)
		builder->connective = tokenConnective;
	else if(builder->connective != tokenConnective)
		return false;

	if(!TermBuilderIsValid(&(builder->termBuilder)))
		return false;
	if(!addCurrentTerm(builder))
		return false;
	builder->isValid = false;
	return true;
}


bool FormulaBuilderIsValid(FormulaBuilder const * builder)
{
	return builder->isValid;
}


bool FormulaBuilderFinish(FormulaBuilder * builder)
{
	ASSERT(builder->isValid)
	// a lone term is held by the term builder and has nothing to finish
	if(builder->connective == CONNECTIVE_NONE)
		return true;

	ASSERT(TermBuilderIsValid(&(builder->termBuilder)));
	builder->isValid = addCurrentTerm(builder);
	return builder->isValid;
}


/*
 * A clause of one term, or a conjunction of one term, says no more than the term
 * itself. The term builder still holds the last term collected, so a formula with
 * no connective is created as that term, and only a formula with a connective is
 * flattened into a clause or a conjunction.
 */
Atom FormulaBuilderCreateFormula(FormulaBuilder * builder)
{
	ASSERT(FormulaBuilderIsValid(builder))
	if(builder->connective == CONNECTIVE_NONE)
		return TermBuilderCreateFormula(&(builder->termBuilder));

	size8 nTerms = builder->terms.nElements;
	Atom const * terms = ResizingArrayGetMemory(&(builder->terms));
	if(builder->connective == CONNECTIVE_AND)
		return CreateConjunction(terms, nTerms);
	return CreateClause(terms, nTerms);
}


void FormulaBuilderReset(FormulaBuilder * builder)
{
	TermBuilderReset(&(builder->termBuilder));
	size8 nTerms = builder->terms.nElements;
	for(index8 i = 0; i < nTerms; i++) {
		Atom term = *((Atom *) ResizingArrayGetElement(&(builder->terms), i));
		ReleaseFormula(term);
	}
	ResizingArrayReset(&(builder->terms));
	builder->arity = 0;
	builder->connective = CONNECTIVE_NONE;
	builder->isValid = false;
}


void CleanupFormulaBuilder(FormulaBuilder * builder)
{
	FormulaBuilderReset(builder);
	TermBuilderFree(&(builder->termBuilder));
	FreeResizingArray(&(builder->terms));
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
	InitializeFormulaBuilder(&builder, FORMULA_TOP_SCOPE);

	Atom formula = (Atom) {0};
	if(tokenizeToFormulaBuilder(cString, &builder, errorPosition)) {
		if(FormulaBuilderIsValid(&builder) && FormulaBuilderFinish(&builder))
			formula = FormulaBuilderCreateFormula(&builder);
		else
			// Either every token was accepted but they do not add up to a formula, or
			// the last term repeats one before it, which is only known once
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
