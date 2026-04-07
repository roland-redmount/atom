/**
 * A token represents a syntactic elements, including names and atoms
 * but also various delimiters.
 */

#ifndef TOKEN_H
#define TOKEN_H

#include "lang/TypedAtom.h"


enum TokenType {
	TOKEN_INVALID = 0,
	TOKEN_NAME,
	TOKEN_NUMBER,	// TODO: should we differentiate between INT and FLOAT ?
	TOKEN_STRING,
	TOKEN_VARIABLE,
	TOKEN_PARAMETER,
	TOKEN_AND,		// logical conjunction (&)
	TOKEN_OR,		// logical disjunction (|)
	TOKEN_NOT		// logical negation
};

typedef struct s_Token {
	enum TokenType type;
	TypedAtom atom;
} Token;


/**
 * A "literal" in this context is a token that be an actor in a role,
 * namely TOKEN_NUMBER, TOKEN_STRING, TOKEN_VARIABLE, TOKEN_PARAMETER
 */
bool TokenIsLiteral(Token token);

/**
 * Release the atom contained in the token, if any.
 */
void ReleaseToken(Token token);

void PrintToken(Token token);

#endif	// TOKEN_H
