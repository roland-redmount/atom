
#include "parser/Characters.h"
#include "parser/Tokenizer.h"
#include "parser/TupleParser.h"


/*
 * The actors read by ParseActors()
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


/* Append the column from start to end to the columns array, without the spaces
   around the column. Returns the number of columns so far. */
static size32 addColumn(
	char const * cString, index32 start, index32 end,
	Column columns[], size32 maxColumns, size32 nColumns)
{
	while((start < end) && IsWhiteSpace(cString[start]))
		start++;
	while((end > start) && IsWhiteSpace(cString[end - 1]))
		end--;
	if(nColumns < maxColumns)
		columns[nColumns] = (Column) {.start = start, .length = end - start};
	return nColumns + 1;
}


size32 SplitColumns(char const * cString, char separator, Column columns[], size32 maxColumns)
{
	size32 nColumns = 0;
	index32 start = 0;
	bool isInString = false;
	for(index32 i = 0; ; i++) {
		char c = cString[i];
		if(c == '"')
			isInString = !isInString;
		bool isEnd = (c == 0) || ((c == separator) && !isInString);
		if(!isEnd)
			continue;

		nColumns = addColumn(cString, start, i, columns, maxColumns, nColumns);
		if(c == 0)
			break;
		start = i + 1;
	}
	return nColumns;
}


bool ParseColumnActor(
	char const * cString, Column column, TypedAtom * actor, bool * isEmpty, index32 * errorIndex)
{
	char columnString[column.length + 1];
	CopyMemory(cString + column.start, columnString, column.length);
	columnString[column.length] = 0;

	size8 nActors;
	if(!ParseActors(columnString, actor, 1, &nActors, errorIndex)) {
		*errorIndex += column.start;
		return false;
	}
	*isEmpty = (nActors == 0);
	return true;
}
