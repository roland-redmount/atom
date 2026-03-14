
#ifndef VM_H
#define VM_H

#include "lang/Atom.h"


/**
 * Initalize the VM stack &c
 */
void VMInitialize(void * stack,  size32 stackSize);


/**
 * Start the virtual machine with the root context
 * executing the given bytecode, with specified actors.
 * For a basic REPL this bytecode would be a loop calling
 * a user input function, e.g.
 * 
 *  CALL [user-input #1>]
 *  ...
 *  RESUME
 * 
 * This takes an array of pointers to datums to allow
 * writing data into output arguments of the given bytecode.
 */
void VMStart(Atom bytecode, Datum ** actors);


#endif	// VM_H
