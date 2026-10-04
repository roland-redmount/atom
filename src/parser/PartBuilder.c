
#include "kernel/ifact.h"
#include "lang/name.h"
#include "lang/Variable.h"
#include "memory/allocator.h"
#include "parser/FormulaBuilder.h"
#include "parser/PartBuilder.h"
#include "parser/Tokenizer.h"


void InitializePartBuilder(PartBuilder * builder, enum FormulaScope scope)
{
	builder->state = STATE_EMPTY;
	builder->scope = scope;
	builder->formulaBuilder = 0;
	// role and actor are undefined
}


/**
 * Whether the actor is a variable written with a quote, as ^x.
 */
static bool isQuotedVariable(TypedAtom actor)
{
	return (actor.type == AT_VARIABLE) && VariableIsQuoted(actor.atom);
}

static void releaseFormulaBuilder(PartBuilder * builder)
{
	CleanupFormulaBuilder(builder->formulaBuilder);
	Free(builder->formulaBuilder);
	builder->formulaBuilder = 0;
}


/**
 * Push a token to a part builder in STATE_REFLECTION.
 */
static bool pushReflectionToken(PartBuilder * builder, Token token)
{
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
		Atom formula = FormulaBuilderCreateFormula(builder->formulaBuilder, 0);
		// the reference from FormulaBuilderCreateFormula() belongs to the actor,
		// so it is not acquired here
		builder->actor = CreateTypedAtom(builder->reflectionType, formula);
		releaseFormulaBuilder(builder);
		// CLAUDE: a relation [[ ... ]] still needs its second ]
		builder->state = (builder->reflectionType == AT_RELATION) ?
			STATE_RELATION_END : STATE_COMPLETE;
		return true;
	}
	else {
		// Any other rejected token means error in the reflection formula
		return false;
	}
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
			// a reflection within a reflection is still a reflection
			InitializeFormulaBuilder(builder->formulaBuilder, FORMULA_REFLECTED_SCOPE);
			builder->reflectionType = AT_FORMULA;
			builder->state = STATE_REFLECTION_START;
			return true;
		}
		// A TOKEN_END_REFLECT is never an actor; see STATE_REFLECTED_NAME
		if(token.type == TOKEN_END_REFLECT)
			return false;
		// A quoted variable cannot occur outside a reflection; see enum FormulaScope.
		if((builder->scope == FORMULA_TOP_SCOPE) && isQuotedVariable(token.typedAtom))
			return false;
		// the tokenizer reads an actor after a role name, so this token is one
		builder->actor = token.typedAtom;
		AcquireTypedAtom(builder->actor);
		builder->state = STATE_COMPLETE;
		return true;

	case STATE_REFLECTION_START:
		// A second [ directly after the first makes the reflection a relation [[ ... ]]
		if(token.type == TOKEN_BEGIN_REFLECT) {
			builder->reflectionType = AT_RELATION;
			builder->state = STATE_REFLECTION;
			return true;
		}
		// A name may be a reflected name [name]; see STATE_REFLECTED_NAME
		if(token.type == TOKEN_NAME) {
			builder->actor = token.typedAtom;
			AcquireTypedAtom(builder->actor);
			builder->state = STATE_REFLECTED_NAME;
			return true;
		}
		// any other token begins the formula of the reflection
		builder->state = STATE_REFLECTION;
		return pushReflectionToken(builder, token);

	case STATE_REFLECTED_NAME:
		if(token.type == TOKEN_END_REFLECT) {
			releaseFormulaBuilder(builder);
			builder->state = STATE_COMPLETE;
			return true;
		}
		// CLAUDE: the held name begins a formula. The nested builder takes its own reference
		// to the name, so the reference held by the actor is released.
		bool isAccepted = FormulaBuilderPush(
			builder->formulaBuilder, (Token) {TOKEN_NAME, builder->actor});
		ASSERT(isAccepted)
		ReleaseTypedAtom(builder->actor);
		builder->state = STATE_REFLECTION;
		return pushReflectionToken(builder, token);

	case STATE_REFLECTION:
		return pushReflectionToken(builder, token);

	case STATE_RELATION_END:
		if(token.type != TOKEN_END_REFLECT)
			return false;
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
	if(builder->state == STATE_HAS_NAME) {
		NameRelease(builder->role);
	}
	else if((builder->state == STATE_REFLECTION_START) || (builder->state == STATE_REFLECTION)) {
		// an unterminated reflection, abandoned with its nested builder
		NameRelease(builder->role);
		releaseFormulaBuilder(builder);
	}
	else if(builder->state == STATE_REFLECTED_NAME) {
		// A reflected name not yet closed, held as the actor
		NameRelease(builder->role);
		ReleaseTypedAtom(builder->actor);
		releaseFormulaBuilder(builder);
	}
	else if((builder->state == STATE_RELATION_END) || (builder->state == STATE_COMPLETE)) {
		NameRelease(builder->role);
		ReleaseTypedAtom(builder->actor);
	}
	builder->state = STATE_EMPTY;
}

