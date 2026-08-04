
/**
 * The dispatcher accepts a query (formula) and finds a matching service
 * within the current process.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"

/**
 * Dispatch a query, returning the matching service, if any.
 * The argument permutation required to match the service is written
 * to the given permutation array, such that queryActors[permutation[i]]
 * matches service parameter i.
 * 
 * NOTE: do we have to return a ServiceRecord? Or just a Service?
 */
bool DispatchQuery(Atom queryTermForm, TypedTuple const * queryActors, ServiceRecord * record, index8 * permutation);

/**
 * As DispatchQuery(), but returns the (skip + 1)-th matching service rather
 * than the first, and reports in *hasMore whether any further match exists
 * beyond the one returned. Pass hasMore = 0 if this is not of interest.
 *
 * Several services may match when the query leaves an output parameter
 * untyped, since the type is then unconstrained: every relation table
 * registered for the form is a candidate. The compiler uses this to compile
 * one service per candidate; see compiler.c.
 */
bool DispatchQueryAt(
	Atom queryTermForm, TypedTuple const * queryActors, ServiceRecord * record,
	index8 * permutation, index8 skip, bool * hasMore);

/**
 * Same, using a term (formula) instead of a termform and actors tuple
 */
bool DispatchQueryFormula(Formula * queryTerm, ServiceRecord * record, index8 * permutation);


#endif	// DISPATCH_H
