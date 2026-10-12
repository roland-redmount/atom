#include "kernel/ifact.h"
#include "kernel/multiset.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "parser/TermFormBuilder.h"
#include "parser/Tokenizer.h"


void InitializeTermFormBuilder(TermFormBuilder * builder)
{
	builder->nNames = 0;
	builder->sign = true;
}


bool TermFormBuilderPush(TermFormBuilder * builder, Token token)
{
	if(token.type == TOKEN_NOT) {
		if(!builder->sign || (builder->nNames > 0))
			return false;
		builder->sign = false;
		return true;
	}
	if((token.type != TOKEN_NAME) || (builder->nNames == RELATION_MAX_ARITY))
		return false;
	NameAcquire(token.typedAtom.atom);
	builder->names[builder->nNames++] = token.typedAtom.atom;
	return true;
}


bool TermFormBuilderIsValid(TermFormBuilder const * builder)
{
	return builder->nNames > 0;
}


Atom TermFormBuilderCreateTermForm(TermFormBuilder const * builder, index8 roleOrder[])
{
	ASSERT(TermFormBuilderIsValid(builder))
	Atom predicateForm = CreatePredicateForm(builder->names, builder->nNames);
	if(roleOrder)
		MultisetIterationOrder(predicateForm, AT_NAME, builder->names, roleOrder, builder->nNames);
	Atom termForm = CreateTermForm(predicateForm, builder->sign);
	IFactRelease(predicateForm);
	return termForm;
}


void CleanupTermFormBuilder(TermFormBuilder * builder)
{
	for(index8 i = 0; i < builder->nNames; i++)
		NameRelease(builder->names[i]);
	builder->nNames = 0;
}


static bool handleTermFormToken(void * context, Token token)
{
	return TermFormBuilderPush(context, token);
}


Atom ParseTermForm(char const * cString, index8 roleOrder[], index32 * errorIndex)
{
	TermFormBuilder builder;
	InitializeTermFormBuilder(&builder);
	Atom termForm = (Atom) {0};
	if(TokenizeCStringInState(cString, TOKENIZER_FORM_STATE, handleTermFormToken, &builder, errorIndex)) {
		if(TermFormBuilderIsValid(&builder))
			termForm = TermFormBuilderCreateTermForm(&builder, roleOrder);
		else
			*errorIndex = CStringLength(cString);
	}
	CleanupTermFormBuilder(&builder);
	return termForm;
}
