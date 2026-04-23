/**
 * A bytecode execution context (a continuation), representing the state of a
 * running bytecode program. This structure is created by the VM CTX instruction.
 * A BytecodeContext * pointer is an AT_CONTEXT atom, stored in VM registers;
 * since no two execution contexts are identical, the pointer identifies the atom.

 */

#ifndef CONTEXT_H
#define CONTEXT_H

#include "kernel/ServiceRegistry.h"
#include "lang/TypedAtom.h"


/**
 * Create a new bytecode context (AT_CONTEXT) for the given bytecode service.
 * The context contains pointers/references to the bytecode program,
 * and the arguments, registers and constands used by the program.
 * Arguments are read/written with VM instructions using context addressing.
 */
Atom CreateBytecodeContext(ServiceRecord * service, Atom parentContext);

/**
 * Create a new compiled context
 */
Atom CreateCompiledContext(ServiceRecord * service);


enum ContextType {
	BYTECODE_CONTEXT = 1,
	COMPILED_CONTEXT = 2
};

enum ContextType ContextGetType(Atom context);

/**
 * Copy all context parameters to the given tuple.
 * Used to fetch results from a root context after YIELD.
 */
void ContextGetParameters(Atom context, Tuple * tuple);

/**
 * Copy the arguments tuple to the context parameters.
 * Used to initialize arguments for a root context.
 */
void ContextSetParameters(Atom context, Tuple const * arguments);

/**
 * Read an operand from a bytecode context, as indicated by the instruction.
 */
Atom ContextReadOperand(Atom context, Instruction inst, Operand operand);


/**
 * Read a typed operand from a bytecode context, as indicated by the instruction.
 */
TypedAtom ContextReadTypedOperand(Atom context, Instruction inst, Operand operand);


/**
 * Write an operand from a bytecode context, as indicated by the instruction.
 */
void ContextWriteOperand(Atom context, Instruction inst, index8 operand, Atom atom);

/**
 * Writing a typed operand is legal only if the destination type matches,
 * except for writes to COMPILED_CONTEXT arguments, which determines their type.
 */
void ContextWriteTypedOperand(Atom context, Instruction inst, Operand operand, TypedAtom typedAtom);


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
 * NOTE: Having reference-counted AT_CONTEXT atoms would render this unnecessary.
 */
void BytecodeContextFreeChildContexts(Atom context);

void FreeContext(Atom context);


#endif	// CONTEXT_H
