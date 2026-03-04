
#ifndef VM_H
#define VM_H

#include "vm/context.h"


void VMInitialize(void * stack,  size32 stackSize);

/**
 * To call a bytecode service, we must create a VM context
 * with program counter, registers and flags.
 * The program will read/write from/to the given actors tuple.
 * The context is stored in the "child" context list
 * of the caller's context (except for the root context).
 * Control (single-threaded for now) passes to the service
 * until a YIELD instruction, or the end of the bytecode program.
 */

/**
 * Push a new context onto the stack and return it.
 */
VMContext * VMCreateContext(Atom bytecode, Datum * actors);


void VMExecute(Atom bytecode, Datum * actors);


#endif	// VM_H
