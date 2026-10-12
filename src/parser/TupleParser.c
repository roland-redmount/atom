
#include "parser/Tokenizer.h"
#include "parser/TupleParser.h"


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
	if(TokenizeCStringInState(cString, TOKENIZER_ACTOR_STATE, handleActorToken, &actorList, errorIndex)) {
		*nActors = actorList.nActors;
		return true;
	}
	for(index8 i = 0; i < actorList.nActors; i++)
		ReleaseTypedAtom(actors[i]);
	*nActors = 0;
	return false;
}
