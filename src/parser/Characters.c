
#include "parser/Characters.h"


bool IsWhiteSpace(char c)
{
	return c == ' ';
}


// these characters cannot occur in syntax
static char const * reservedChars = (char const *) "():;.,`'";

// valid characters in syntax
bool IsSyntaxChar(char c)
{
	return IsPrintableChar(c) && !CStringFindChar(reservedChars, c);
}

// forms can contain any syntax character
bool IsFormChar(char c)
{
	return IsSyntaxChar(c);
}

// clauses cannot contain &
bool IsClauseChar(char c)
{
	return IsFormChar(c) && c != '&';
}

// predicates cannot contain '|'
bool IsPredicateChar(char c)
{
	return IsClauseChar(c) && c != '|';
}

// terms cannot spaces ' '
bool IsRoleChar(char c)
{
	return IsPredicateChar(c) && c != ' ';
}

static char const * separatorChars = (char const *) "&|!_$\" ";

// names cannot contain separator characters
bool IsNameChar(char c)
{
	return IsSyntaxChar(c) && !CStringFindChar(separatorChars, c);
}

// names cannot start with a digit
bool IsNameInitialChar(char c)
{
	return IsNameChar(c) && !IsDigitChar(c);
}

// clause, predicate or role separator character
bool IsSeparatorChar(char c)
{
	return c == ' ' || c == '|' || c == '&';
}


bool IsNameString(char const * string, size32 length)
{
	for(index32 i = 0; i < length; i++) {
		if(!IsNameChar(string[i]))
			return false;
	}
	return true;
}

/**
 * like strchr() but does not assume a terminating '\0'
 */
index32 FindCharIndex(char const * string, size32 length, char c)
{
	for(int i = 0; i < length; i++) {
		if(string[i] == c)
			return i;
	}
	return INDEX_INFINITY;
}

bool StringContainsChar(char const * string, size32 length, char c)
{
	return FindCharIndex(string, length, c) != INDEX_INFINITY;
}
