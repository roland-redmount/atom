
#ifndef VM_H
#define VM_H

#include "kernel/ServiceRegistry.h"

/**
 * Initalize the VM
 */
void VMInitialize(void);


/**
 * Execute a bytecode service with the given arguments,
 * returing at most one tuple.
 * Returns true of the service executed YIELD, in which case
 * output parameters are written to the argument array.
 */
bool VMExecuteService(ServiceRecord * service, Tuple * arguments);

/**
 * Initialize a VM context for iteration.
 * Returns an AT_CONTEXT
 */
Atom VMBeginService(ServiceRecord * service, Tuple * arguments);

/**
 * Call a VM service. Returns true if the service yielded,
 * in which case the result tuple can be access with ContextGetParameters().
 * Returns false if the service terminated, in which case the context
 * is deallocated.
 */
bool VMCall(Atom context);


#endif	// VM_H
