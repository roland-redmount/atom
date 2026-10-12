
#include "kernel/ifact.h"
#include "lang/name.h"
#include "lang/Variable.h"
#include "memory/allocator.h"
#include "parser/FormulaBuilder.h"
#include "parser/PartBuilder.h"
#include "parser/TermFormBuilder.h"
#include "parser/Tokenizer.h"


void InitializePartBuilder(PartBuilder * builder, enum FormulaScope scope)
{
	builder->state = STATE_EMPTY;
	builder->scope = scope;
	builder->formulaBuilder = 0;
	builder->termFormBuilder = 0;
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
 * Begin a reflected formula or relation, collected by a nested formula builder.
 * The reflectionType is either AT_FORMULA or AT_RELATION.
 */
static void beginReflection(PartBuilder * builder, byte reflectionType, enum BuilderState state)
{
	builder->formulaBuilder = Allocate(sizeof(FormulaBuilder));
	// a reflection within a reflection is still a reflection
	InitializeFormulaBuilder(builder->formulaBuilder, FORMULA_REFLECTED_SCOPE);
	builder->reflectionType = reflectionType;
	builder->state = state;
}


static void releaseTermFormBuilder(PartBuilder * builder)
{
	CleanupTermFormBuilder(builder->termFormBuilder);
	Free(builder->termFormBuilder);
	builder->termFormBuilder = 0;
}


/**
 * Push a token to a part builder in STATE_REFLECTED_FORM. The TOKEN_END_REFLECT
 * closing the form makes the term form the actor.
 */
static bool pushReflectedFormToken(PartBuilder * builder, Token token)
{
	if(token.type == TOKEN_END_REFLECT) {
		if(!TermFormBuilderIsValid(builder->termFormBuilder))
			return false;
		Atom termForm = TermFormBuilderCreateTermForm(builder->termFormBuilder, 0);
		// CLAUDE: the reference from TermFormBuilderCreateTermForm() belongs to the actor
		builder->actor = CreateTypedAtom(AT_ID, termForm);
		releaseTermFormBuilder(builder);
		builder->state = STATE_COMPLETE;
		return true;
	}
	return TermFormBuilderPush(builder->termFormBuilder, token);
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
		builder->state = STATE_COMPLETE;
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
			beginReflection(builder, AT_FORMULA, STATE_REFLECTION_START);
			return true;
		}
		// A relation [: ... ] holds a formula, and never a name
		if(token.type == TOKEN_BEGIN_RELATION) {
			beginReflection(builder, AT_RELATION, STATE_REFLECTION);
			return true;
		}
		if(token.type == TOKEN_BEGIN_FORM) {
			builder->termFormBuilder = Allocate(sizeof(TermFormBuilder));
			InitializeTermFormBuilder(builder->termFormBuilder);
			builder->state = STATE_REFLECTED_FORM;
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

	case STATE_REFLECTED_FORM:
		return pushReflectedFormToken(builder, token);

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
	else if(builder->state == STATE_REFLECTED_FORM) {
		// An unterminated reflected form, abandoned with its nested builder
		NameRelease(builder->role);
		releaseTermFormBuilder(builder);
	}
	else if(builder->state == STATE_COMPLETE) {
		NameRelease(builder->role);
		ReleaseTypedAtom(builder->actor);
	}
	builder->state = STATE_EMPTY;
}

