
/**
 * The dispatcher accepts a query (formula) and finds a matching service
 * within the current process.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/ServiceRegistry.h"


/**
 * Dispatch a query (formula), return the matching service, if any.
 * The argument permutation required to match the service is written
 * to the given permutation array.
 */
bool DispatchQuery(Atom query, ServiceRecord * record, index8 * permutation);


#endif	// DISPATCH_H
