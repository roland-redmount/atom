
/**
 * The dispatcher accepts a query (formula) and finds a matching service
 * within the current process.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/ServiceRegistry.h"


/**
 * Dispatch a query, returning the matching service, if any.
 * The argument permutation required to match the service is written
 * to the given permutation array, such that queryActors[permutation[i]]
 * matches service parameter i
 */
bool DispatchQuery(Atom queryTermForm, TypedTuple const * queryActors, ServiceRecord * record, index8 * permutation);

/**
 * Same, using a term (formula) instead of a termform and actors tuple
 */
bool DispatchQueryFormula(Atom queryTerm, ServiceRecord * record, index8 * permutation);


#endif	// DISPATCH_H
