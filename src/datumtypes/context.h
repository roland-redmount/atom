/**
 * A bytecode execution context (a continuation).
 * 
 * VM registers can store a BytecodeContext * pointer,
 * and then use atom type AT_CONTEXT. This atom type are not used
 * in any other sitations; we may consider removing it from the atom type list
 * as the type is implied by CTX and CALL instructions anyway.
 * 
 * No two execution contexts are identical, so we can compare pointers directly
 * (the address is a unique identifier for a context).
 * 
 * TODO: we need a context for C-level (machine code) services as well.
 * They also need an argument vector + a state memory block.
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "kernel/ServiceRegistry.h"
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
 * Create a new execution context for the given bytecode service. 
 */
BytecodeContext * CreateBytecodeContext(ServiceRecord * service, BytecodeContext * parentContext);

/**
 * Check registers for allocated "child" contexts and free them if necessary.
 * This is used when terminating context execution.
 */
void FreeChildContexts(BytecodeContext * context);


void FreeContext(BytecodeContext * context);


#endif	// CONTEXT_H
