
#include "kernel/ifact.h"
#include "kernel/string.h"
#include "lang/name.h"
#include "memory/allocator.h"
#include "parser/FormulaBuilder.h"
#include "parser/PartBuilder.h"
#include "parser/Tokenizer.h"


void InitializePartBuilder(PartBuilder * builder)
{
	builder->state = STATE_EMPTY;
	builder->formulaBuilder = 0;
	// role and actor are undefined
}

static void releaseFormulaBuilder(PartBuilder * builder)
{
	CleanupFormulaBuilder(builder->formulaBuilder);
	Free(builder->formulaBuilder);
	builder->formulaBuilder = 0;
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
		builder->state = STATE_HAS_NAME;
		return true;	

	case STATE_HAS_NAME:
		if(token.type == TOKEN_BEGIN_REFLECT) {
			builder->formulaBuilder = Allocate(sizeof(FormulaBuilder));
			InitializeFormulaBuilder(builder->formulaBuilder);
			builder->state = STATE_REFLECTION;
			return true;
		}
		// the tokenizer reads an actor after a role name, so this token is one
		builder->actor = token.typedAtom;
		AcquireTypedAtom(builder->actor);
		builder->state = STATE_COMPLETE;
		return true;

	case STATE_REFLECTION:
		// The nested builder is offered the token first. A reflection within this
		// one is closed by the part builder that opened it, so a TOKEN_END_REFLECT
		// the nested builder rejects can only be the one closing this reflection.
		if(FormulaBuilderPush(builder->formulaBuilder, token))
			return true;	// token accepted by reflection's formula builder
		if(token.type == TOKEN_END_REFLECT) {
			if(!FormulaBuilderIsValid(builder->formulaBuilder))
				return false;
			// Check that the completed reflected formula is valid
			if(!FormulaBuilderFinish(builder->formulaBuilder))
				return false;
			// Create the reflected formula
			Atom formula = FormulaBuilderCreateFormula(builder->formulaBuilder);
			// the reference from FormulaBuilderCreateFormula() belongs to the actor,
			// so it is not acquired here
			builder->actor = CreateTypedAtom(AT_FORMULA, formula);
			releaseFormulaBuilder(builder);
			builder->state = STATE_COMPLETE;
			return true;
		}
		else {
			// Any other rejected token means error in the reflection formula
			return false;
		}

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
	if(builder->state == STATE_HAS_NAME) {
		NameRelease(builder->role);
	}
	else if(builder->state == STATE_REFLECTION) {
		// an unterminated reflection, abandoned with its nested builder
		NameRelease(builder->role);
		releaseFormulaBuilder(builder);
	}
	else if(builder->state == STATE_COMPLETE) {
		NameRelease(builder->role);
		ReleaseTypedAtom(builder->actor);
	}
	builder->state = STATE_EMPTY;
}

