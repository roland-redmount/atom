
#include "kernel/ifact.h"
#include "lang/name.h"
#include "parser/Token.h"
#include "parser/Characters.h"


void ReleaseToken(Token token)
{
	ReleaseTypedAtom(token.typedAtom);
}
