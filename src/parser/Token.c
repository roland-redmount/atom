
#include "kernel/ifact.h"
#include "lang/name.h"
#include "parser/Token.h"
#include "parser/Characters.h"


bool TokenIsLiteral(Token token)
{
	return (token.type == TOKEN_STRING) ||
		(token.type == TOKEN_NUMBER) ||
		(token.type == TOKEN_VARIABLE) ||
		(token.type == TOKEN_PARAMETER);
}


void ReleaseToken(Token token)
{
	if(token.type == TOKEN_NAME) {
		ASSERT(token.typedAtom.type == AT_NAME)
		NameRelease(token.typedAtom.atom);
	}
	if(token.type == TOKEN_STRING) {
		ASSERT(token.typedAtom.type == AT_ID)
		IFactRelease(token.typedAtom.atom);
	}
	// other token types have nothing to release
}

void PrintToken(Token token)
{
	switch(token.type) {
	case TOKEN_AND:
		PrintCString("TOKEN_AND");
		// PrintChar('&');
		break;

	case TOKEN_OR:
		PrintCString("TOKEN_OR");
		// PrintChar('|');
		break;

	case TOKEN_NOT:
		PrintCString("TOKEN_NOT");
		PrintChar('~');
		break;

	case TOKEN_NAME:
	case TOKEN_NUMBER:
	case TOKEN_VARIABLE:
	case TOKEN_STRING:
		PrintTypedAtom(token.typedAtom);

	default:
		PrintF("Token type %u", token.type);
	}
}
