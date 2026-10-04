
#include "kernel/ifact.h"
#include "kernel/multiset.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "parser/FormTupleParsers.h"
#include "parser/Tokenizer.h"


/*
 * CLAUDE: Push every character of the string to a tokenizer reading every token in the
 * given state, and offer each completed token to the handler. The token is released once
 * the handler has processed it. Returns false at the first character the tokenizer
 * rejects, or the first token the handler rejects, and writes the index where the
 * offending token begins to errorIndex. See tokenizeToFormulaBuilder() in FormulaBuilder.c,
 * which reads tokens in the states that the syntax of a formula leads to.
 */
static bool tokenizeInState(
	char const * cString, enum TokenizerState state,
	TokenHandler handler, void * context, index32 * errorIndex)
{
	Tokenizer tokenizer;
	TokenizerInit(&tokenizer, TOKENIZER_STRING_INPUT);
	TokenizerRestart(&tokenizer, state);
	size32 length = CStringLength(cString);
	index32 tokenIndex = 0;
	bool isAccepted = true;

	// CLAUDE: including the 0 terminator, which completes the last token
	for(index32 i = 0; i <= length; i++) {
		if(!tokenizer.type)
			tokenIndex = i;

		enum TokenizerResult result = TokenizerPush(&tokenizer, cString[i]);
		if(result == TOKENIZER_REJECTED) {
			*errorIndex = i;
			isAccepted = false;
			break;
		}
		if(result == TOKENIZER_ENDED) {
			// CLAUDE: the character is pushed again below, after the token is handled
			i--;
		}
		if(TokenizerIsFull(&tokenizer)) {
			Token token = TokenizerGetToken(&tokenizer);
			isAccepted = handler(context, token);
			ReleaseToken(token);
			if(!isAccepted) {
				*errorIndex = tokenIndex;
				break;
			}
			// CLAUDE: TokenizerReset() keeps the separator rule of enum TokenizerInputMode,
			// but follows the formula syntax to the next state. Every token here is read
			// in the same state instead.
			TokenizerReset(&tokenizer);
			tokenizer.state = state;
		}
	}
	TokenizerFree(&tokenizer);
	return isAccepted;
}


/*
 * CLAUDE: The role names read by ParseTermForm(), and the sign given by a leading !
 */
typedef struct s_RoleNames {
	Atom names[RELATION_MAX_ARITY];
	size8 nNames;
	bool sign;
	bool hasNot;
} RoleNames;


static bool handleRoleToken(void * context, Token token)
{
	RoleNames * roleNames = context;
	if(token.type == TOKEN_NOT) {
		if(roleNames->hasNot || (roleNames->nNames > 0))
			return false;
		roleNames->hasNot = true;
		roleNames->sign = false;
		return true;
	}
	if((token.type != TOKEN_NAME) || (roleNames->nNames == RELATION_MAX_ARITY))
		return false;
	NameAcquire(token.typedAtom.atom);
	roleNames->names[roleNames->nNames++] = token.typedAtom.atom;
	return true;
}


Atom ParseTermForm(char const * cString, index8 roleOrder[], index32 * errorIndex)
{
	RoleNames roleNames = {.nNames = 0, .sign = true, .hasNot = false};
	Atom termForm = (Atom) {0};
	if(tokenizeInState(cString, TOKENIZER_ROLE_STATE, handleRoleToken, &roleNames, errorIndex)) {
		if(roleNames.nNames == 0)
			*errorIndex = CStringLength(cString);
		else {
			Atom predicateForm = CreatePredicateForm(roleNames.names, roleNames.nNames);
			MultisetIterationOrder(
				predicateForm, AT_NAME, roleNames.names, roleOrder, roleNames.nNames);
			termForm = CreateTermForm(predicateForm, roleNames.sign);
			IFactRelease(predicateForm);
		}
	}
	for(index8 i = 0; i < roleNames.nNames; i++)
		NameRelease(roleNames.names[i]);
	return termForm;
}


/*
 * CLAUDE: The actors read by ParseActors()
 */
typedef struct s_ActorList {
	TypedAtom * actors;
	size8 maxActors;
	size8 nActors;
} ActorList;


static bool handleActorToken(void * context, Token token)
{
	ActorList * actorList = context;
	switch(token.type) {
	case TOKEN_STRING:
	case TOKEN_NUMBER:
	case TOKEN_LETTER:
	case TOKEN_ID:
		break;
	default:
		return false;
	}
	if(actorList->nActors == actorList->maxActors)
		return false;
	AcquireTypedAtom(token.typedAtom);
	actorList->actors[actorList->nActors++] = token.typedAtom;
	return true;
}


bool ParseActors(
	char const * cString, TypedAtom actors[], size8 maxActors, size8 * nActors, index32 * errorIndex)
{
	ActorList actorList = {.actors = actors, .maxActors = maxActors, .nActors = 0};
	if(tokenizeInState(cString, TOKENIZER_ACTOR_STATE, handleActorToken, &actorList, errorIndex)) {
		*nActors = actorList.nActors;
		return true;
	}
	for(index8 i = 0; i < actorList.nActors; i++)
		ReleaseTypedAtom(actors[i]);
	*nActors = 0;
	return false;
}
