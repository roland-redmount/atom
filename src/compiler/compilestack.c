
#include "compiler/compilestack.h"
#include "kernel/Parameter.h"


void CompileStackAdd(CompileStack * stack, FormulaView term)
{
	ASSERT(stack->depth < MAX_COMPILE_STACK_DEPTH)
	stack->terms[stack->depth++] = term;
}


void CompileStackRemove(CompileStack * stack)
{
	ASSERT(stack->depth > 0)
	stack->depth--;
}


bool CompileStackContainsTerm(CompileStack const * stack, FormulaView term)
{
	for(index8 i = 0; i < stack->depth; i++) {
		if(SameAtoms(stack->terms[i].form, term.form)
			&& SameParameterSignature(stack->terms[i].actors, term.actors))
			return true;
	}
	return false;
}
