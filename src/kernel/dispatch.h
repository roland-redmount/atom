
/**
 * The dispatcher accepts a query (formula) and finds a matching service
 * within the current process.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/ServiceRegistry.h"


/**
 * Dispatch a query (formula), return the matching service, if any.
 * TODO: should return the permuted arguments
 */
bool DispatchQuery(Atom query, ServiceRecord * record, Tuple * arguments);


#endif	// DISPATCH_H
