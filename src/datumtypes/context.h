/**
 * DT_CONTEXT reprents a VM execution context structure (a continuation).
 * is probably to Allocate() it and store the pointer in the Datum.
 * No two execution contexts are identical, so we can compare pointers directly
 * (the address is a unique identifier for a context).
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "lang/Atom.h"


typedef struct s_VMContext VMContext;

struct s_VMContext {
	VMContext * parentContext;
	Atom bytecode;
	Atom program;				// list of instructions
	size32 programLength;
	index32 programCounter;
	size8 arity;
	size8 nRegisters;
	// Datum arguments[arity]
	// Datum registers[nRegisters]
};


/**
 * Context size, including registers and child context pointers,
 * but excluding actors
 */
size32 ContextSize(size8 arity, size8 nRegisters);

/**
 * Return arguments array
 */
Datum * ContextArguments(VMContext * context);

/**
 * Return register at the given index (1-based)
 */
Datum * ContextRegisters(VMContext * context);

/**
 * Create a new execution context for the given bytecode program on the top of the stack. 
 * The program arguments must have been pushed prior to calling this function.
 * The child context contains pointers to the bytecode program
 * and a working copy of the registers used.
 */
VMContext * CreateContext(Atom bytecode, VMContext * parentContext);

/**
 * Check registers for allocated "child" contexts and free them if necessary.
 * This is used when terminating context execution.
 */
void FreeChildContexts(VMContext * context);


void FreeContext(VMContext * context);


#endif	// CONTEXT_H
