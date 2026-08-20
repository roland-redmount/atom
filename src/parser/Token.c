
#include "kernel/ifact.h"
#include "lang/name.h"
#include "parser/Token.h"
#include "parser/Characters.h"


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
