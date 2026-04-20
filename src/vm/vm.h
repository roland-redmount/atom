
#ifndef VM_H
#define VM_H

#include "kernel/ServiceRegistry.h"

/**
 * Initalize the VM
 */
void VMInitialize(void);


/**
 * Create a root context from the given bytecode, with given arguments.
 * Arguments are given as an array of pointers to atoms to allow
 * writing data into output arguments of the given bytecode.
 */
Atom VMCreateRootContext(ServiceRecord * service, Atom * arguments);

/**
 * Start VM execution with the given context as root context.
 */
void VMExecute(Atom context);


#endif	// VM_H
