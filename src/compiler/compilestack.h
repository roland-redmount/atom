/**
 * The compile stack.
 * 
 * A rule body term with no matching service will itself be compiled by compileTerm(),
 * which for cross-recursive rules may lead to a term that is already being compiled.
 * For example, the rules (p x <- q x) and (q x <- p x) recurse through one another.
 * 
 * A CompileStack holds the parameterized queries being compiled, outermost first. 
 * Attempting to re-compile a parameterized query already on this stack yields no service;
 * see compileParameterizedQuery()
 *
 * Recursion through a term the same form as the query is a different matter, and is handled by
 * the recursive pass compileQueryClauses().
 */

#ifndef COMPILE_STACK_H
#define COMPILE_STACK_H

#include "lang/formula.h"


#define MAX_COMPILE_STACK_DEPTH	16

typedef struct s_CompileStack {
	FormulaView terms[MAX_COMPILE_STACK_DEPTH];
	size8 depth;
} CompileStack;


void CompileStackAdd(CompileStack * stack, FormulaView term);

/**
 * Remove the term added last.
 */
void CompileStackRemove(CompileStack * stack);

bool CompileStackContainsTerm(CompileStack const * stack, FormulaView term);


#endif // COMPILE_STACK_H
