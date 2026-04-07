/**
 * DT_CONTEXT reprents a bytecode execution context (a continuation).
 * No two execution contexts are identical, so we can compare pointers directly
 * (the address is a unique identifier for a context).
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "lang/TypedAtom.h"


typedef struct s_BytecodeContext BytecodeContext;

struct s_BytecodeContext {
	BytecodeContext * parentContext;
	Atom bytecode;
	Atom program;				// list of instructions
	size32 programLength;
	index32 programCounter;
	size8 arity;
	size8 nRegisters;
	// Atom arguments[arity]
	// Atom registers[nRegisters]
};


/**
 * Context size, including registers and child context pointers,
 * but excluding actors
 */
size32 ContextSize(size8 arity, size8 nRegisters);

/**
 * Return arguments array
 */
Atom * ContextArguments(BytecodeContext * context);

/**
 * Return register at the given index (1-based)
 */
Atom * ContextRegisters(BytecodeContext * context);

/**
 * Create a new execution context for the given bytecode program on the top of the stack. 
 * The program arguments must have been pushed prior to calling this function.
 * The child context contains pointers to the bytecode program
 * and a working copy of the registers used.
 */
BytecodeContext * CreateContext(Atom bytecode, BytecodeContext * parentContext);

/**
 * Check registers for allocated "child" contexts and free them if necessary.
 * This is used when terminating context execution.
 */
void FreeChildContexts(BytecodeContext * context);


void FreeContext(BytecodeContext * context);


#endif	// CONTEXT_H
