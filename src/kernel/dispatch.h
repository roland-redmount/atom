
/**
 * The dispatcher accepts a query (formula) and finds a matching service
 * within the current process.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"

/**
 * Dispatch a query, returning the first matching service, if any.
 * The argument permutation required to match the service is written
 * to the given permutation array, such that queryActors element permutation[i]
 * matches service parameter i.
 * 
 * NOTE: do we have to return an Service? Or just an Service?
 */
bool DispatchQuery(Atom queryTermForm, TypedTuple const * queryActors, Service * service, index8 permutation[]);

/**
 * Similar to DispatchQuery(), but skips the nSkip first matching services instead of
 * returning the first service, and sets *hasNextMatch = true if at least one additional match exists
 * beyond the one returned. Caller can pass hasNextMatch = 0 if only one match is required.
 *
 * Several services may match when the query leaves an output parameter
 * untyped, since the type is then unconstrained: every relation table
 * registered for the form is a candidate. The compiler uses this to compile
 * one service per candidate; see compiler.c.
 */
bool DispatchQueryAt(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service,
	index8 permutation[], size8 nSkip, bool * hasNextMatch);

/**
 * Same, using a term (formula) instead of a termform and actors tuple
 */
bool DispatchQueryFormula(Formula * queryTerm, Service * service, index8 * permutation);


#endif	// DISPATCH_H
