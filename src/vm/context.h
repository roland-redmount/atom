/**
 * A bytecode execution context (a continuation), representing the state of a
 * running bytecode program. This structure is created by the VM CTX instruction.
 * A BytecodeContext * pointer is an AT_BCONTEXT atom, stored in VM registers;
 * since no two execution contexts are identical, the pointer identifies the atom.
 * 
 * TODO: rename this file to vm/context.h
 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"


/**
 * Create a new bytecode context (AT_BCONTEXT) for the given bytecode service.
 * The context contains pointers/references to the bytecode program,
 * and the arguments, registers and constands used by the program.
 * Arguments are read/written with VM instructions using context addressing.
 */
Atom CreateBytecodeContext(ServiceRecord * service, Atom parentContext);

/**
 * Create a new compiled context
 */
Atom CreateCompiledContext(ServiceRecord * service);

// void ContextAcquire(Atom context);

// void ContextRelease(Atom context);


/**
 * Get a parameter value. The index is 0-based.
 */
Atom ContextGetParameter(Atom context, index8 i);

/**
 * Set a specific parameter to a given argument.  The index is 0-based.
 * Used to initialize arguments for a root context.
 */
void ContextSetParameter(Atom context, index8 i, Atom argument);


Atom ContextReadOperand(Atom context, Instruction inst, Operand operand);

void ContextWriteOperand(Atom context, Instruction inst, index8 operand, Atom atom);


/**
 * Context size, including registers and child context pointers,
 * but excluding actors
 */
// size32 BytecodeContextSize(size8 arity, size8 nRegisters);

/**
 * Return the parent context. For the root context, returns 0
 */
Atom BytecodeContextGetParent(Atom context);

void BytecodeContextSetParent(Atom context, Atom parentContext);

/**
 * Retrieve the next instruction, writing to the given atom.
 * If at end of program, returns false.
 */
bool BytecodeContextNextInstruction(Atom context, Atom * instruction);


bool CompiledContextCall(Atom context);


/**
 * Check registers for allocated "child" contexts and free them if necessary.
 * This is used when terminating context execution.
 * NOTE: Having reference-counted AT_BCONTEXT atoms would render this unnecessary.
 */
void BytecodeContextFreeChildContexts(Atom context);

void FreeContext(Atom context);


#endif	// CONTEXT_H
