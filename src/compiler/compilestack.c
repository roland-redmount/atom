
#include "compiler/compilestack.h"
#include "compiler/compileutil.h"


void CompileStackAdd(CompileStack * stack, FormulaView term)
{
	ASSERT(stack->compileStackDepth < MAX_COMPILE_STACK_DEPTH)
	stack->compilationStack[stack->compileStackDepth++] = term;
}


bool CompileStackContainsTerm(CompileStack const * stack, FormulaView term)
{
	for(index8 i = 0; i < stack->compileStackDepth; i++) {
		if(SameAtoms(stack->compilationStack[i].form, term.form)
			&& SameParameterSignature(stack->compilationStack[i].actors, term.actors))
			return true;
	}
	return false;
}