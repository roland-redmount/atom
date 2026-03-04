/**
 * A bytecode execution context, representing one executing thread.
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "lang/Atom.h"


typedef struct s_VMContext
{
	Atom bytecode;

	// program counter, index into instruction list
	index8 pc;
	
	// the control flag
	// NOTE: should this be local to the context or VM global?
	bool flag;

	// whether to trace instructions
	bool debug;

	size8 arity;
	Datum registers[];
} VMContext;


#endif	// CONTEXT_H
