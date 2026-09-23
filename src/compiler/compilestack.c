
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


bool CompileStackContainsTerm(CompileStack const * stack, ParameterizedQuery const * query)
{
	for(index8 i = 0; i < stack->depth; i++) {
		if(!SameAtoms(stack->queries[i].termForm, query->termForm))
			continue;
		ASSERT(stack->queries[i].arity == query->arity)
		if(SameParameterSignature(stack->queries[i].parameters, query->parameters, query->arity)
			&& SameParameterRepeats(stack->queries[i].parameters, query->parameters, query->arity))
			return true;
	}
	return false;
}
