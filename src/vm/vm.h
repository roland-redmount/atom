
#ifndef VM_H
#define VM_H

#include "datumtypes/context.h"

/**
 * Initalize the VM stack &c
 */
void VMInitialize(void * stack,  size32 stackSize);


/**
 * Create a root context from the given bytecode, with given arguments.
 * Arguments are given as an array of pointers to datums to allow
 * writing data into output arguments of the given bytecode.
 * 
 * For a basic REPL the bytecode could be a loop calling
 * a user input function, e.g.
 * 
 *  CALL [user-input #1>]
 *  ...
 *  RESUME
 * 
 */
VMContext * VMCreateRootContext(Atom bytecode, Datum * arguments);

/**
 * Start VM execution with the given context as root context.
 */
void VMStart(VMContext * context);


#endif	// VM_H
