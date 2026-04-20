/**
 * A bytecode execution context (a continuation), representing the state of a
 * running bytecode program. This structure is created by the VM CTX instruction.
 * A BytecodeContext * pointer is an AT_CONTEXT atom, stored in VM registers;
 * since no two execution contexts are identical, the pointer identifies the atom.
 * 
 * TODO: rename this file to vm/bytecodecontext.h
 */

#ifndef BYTECODECONTEXT_H
#define BYTECODECONTEXT_H

#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"


/**
 * Create a new execution context for the given bytecode service.
 * The context contains pointers/references to the bytecode program,
 * and the arguments, registers and constands used by the program.
 * Arguments are read/written with VM instructions using context addressing.
 */
Atom CreateBytecodeContext(ServiceRecord * service, Atom parentContext);

// void ContextAcquire(Atom context);

// void ContextRelease(Atom context);

/**
 * Return the parent context. For the root context, returns 0
 */
Atom BytecodeContextGetParent(Atom context);

// could be done when creating?
void BytecodeContextSetParent(Atom context, Atom parentContext);

/**
 * Context size, including registers and child context pointers,
 * but excluding actors
 */
size32 BytecodeContextSize(size8 arity, size8 nRegisters);

/**
 * Return arguments array
 */
Atom * BytecodeContextArguments(Atom context);

size8 BytecodeContextNArguments(Atom context);

/**
 * Return registers array
 */
Atom * BytecodeContextRegisters(Atom context);

/**
 * Return constants array
 */
Atom * BytecodeContextConstants(Atom context);

/**
 * Retrieve the next instruction, writing to the given atom.
 * If at end of program, returns false.
 */
bool BytecodeContextNextInstruction(Atom context, Atom * instruction);

/**
 * Check registers for allocated "child" contexts and free them if necessary.
 * This is used when terminating context execution.
 * NOTE: Having reference-counted AT_CONTEXT atoms would render this unnecessary.
 */
void BytecodeContextFreeChildContexts(Atom context);

void FreeBytecodeContext(Atom context);


#endif	// BYTECODECONTEXT_H
