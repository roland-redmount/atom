
#include "kernel/ifact.h"
#include "kernel/string.h"
#include "lang/name.h"
#include "parser/PartBuilder.h"
#include "parser/Tokenizer.h"


void InitializePartBuilder(PartBuilder * builder)
{
	builder->state = STATE_EMPTY;
	// role and actor are undefined
}


bool PartBuilderPush(PartBuilder * builder, Token token)
{
	switch(builder->state) {
	case STATE_EMPTY:
		if(token.type != TOKEN_NAME)
			return false;
		ASSERT(token.typedAtom.type == AT_NAME)
		builder->role = token.typedAtom.atom;
		NameAcquire(builder->role);
		builder->state = STATE_PARTIAL;
		return true;	

	case STATE_PARTIAL:
		if(!TokenIsLiteral(token))
			return false;
		builder->actor = token.typedAtom;
		AcquireTypedAtom(builder->actor);
		builder->state = STATE_COMPLETE;
		return true;

	case STATE_COMPLETE:
		// cannot accept more tokens
		return false;

	default:
		ASSERT(false);
		return false;
	}
}


bool PartBuilderIsEmpty(PartBuilder const * builder)
{
	return builder->state == STATE_EMPTY;
}


bool PartBuilderComplete(PartBuilder const * builder)
{
	return builder->state == STATE_COMPLETE;
}


Atom PartBuilderGetRole(PartBuilder const * builder)
{
	return builder->role;
}


TypedAtom PartBuilderGetActor(PartBuilder const * builder)
{
	return builder->actor;
}


void PartBuilderReset(PartBuilder * builder)
{
	if(builder->state == STATE_PARTIAL) {
		NameRelease(builder->role);
	}
	else if(builder->state == STATE_COMPLETE) {
		NameRelease(builder->role);
		ReleaseTypedAtom(builder->actor);
	}
	builder->state = STATE_EMPTY;
}

