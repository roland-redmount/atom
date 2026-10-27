
#include "compiler/compilestack.h"
#include "kernel/Parameter.h"


void CompileStackPush(CompileStack * stack, ParameterizedQuery const * query)
{
	ASSERT(stack->depth < MAX_COMPILE_STACK_DEPTH)
	stack->queries[stack->depth++] = *query;
}


void CompileStackPop(CompileStack * stack)
{
	ASSERT(stack->depth > 0)
	stack->depth--;
}


/*
 * CLAUDE: Test whether two parameterized queries may compile services of one typed relation.
 * The queries have the same form, the same IO direction of each parameter and repeat
 * parameters at the same positions, and each parameter type is either unknown in one of
 * the queries or the same in both. A parameter of unknown type covers every type it may
 * be resolved to. For example, (reach #1> to #2<ID) overlaps (reach #1>INT to #2<ID).
 */
static bool queriesOverlap(ParameterizedQuery const * query1, ParameterizedQuery const * query2)
{
	if(!SameAtoms(query1->form, query2->form))
		return false;
	ASSERT(query1->arity == query2->arity)
	for(index8 i = 0; i < query1->arity; i++) {
		Atom parameter1 = query1->parameters[i];
		Atom parameter2 = query2->parameters[i];
		if(parameter1.parameter.io != parameter2.parameter.io)
			return false;
		if(parameter1.parameter.atomType && parameter2.parameter.atomType
			&& (parameter1.parameter.atomType != parameter2.parameter.atomType))
			return false;
	}
	return SameParameterRepeats(query1->parameters, query2->parameters, query1->arity);
}


bool CompileStackContainsTerm(CompileStack const * stack, ParameterizedQuery const * query)
{
	for(index8 i = 0; i < stack->depth; i++) {
		if(queriesOverlap(&(stack->queries[i]), query))
			return true;
	}
	return false;
}


bool CompileStackContainsForm(CompileStack const * stack, Atom form)
{
	for(index8 i = 0; i < stack->depth; i++) {
		if(SameAtoms(stack->queries[i].form, form))
			return true;
	}
	return false;
}
