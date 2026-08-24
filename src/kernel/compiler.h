

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/ServiceRegistry.h"
#include "lang/formula.h"

// Upper bound on the number of services one query may compile to
#define MAX_COMPILED_SERVICES	8

/**
 * Attempt to compile a query, registering every generated service.
 * If the services[] array is not 0, a copy of each compiled service is written to it;
 * at most MAX_COMPILED_SERVICES are written.
 * Returns the number of services generated.
 * The service(s) to be compiled must not already exist before this call.
 *
 * A query compiles to multiple services if at any point during the compilation
 * several services with same form but distinct types are matched. For example,
 * the query (list <my_list> position p element e)
 * compiles to one service per element type. Callers enumerating
 * results must therefore iterate over all returned services.
 */
size8 CompileQuery(Atom queryTerm, Service services[]);

/**
 * Find the service answering a query, compiling it from the rules if none is registered
 * yet, and copy it to *service. Returns true if a service was found. The permutation
 * array receives the argument permutation of the match, as for DispatchQuery().
 *
 * Whether a query has been compiled before is decided by dispatching it, so a query
 * compiles once and is answered by the registered service from then on. A query the rules
 * do not answer compiles to nothing and is compiled again every time it is asked, which
 * costs a walk over the rules and is what lets it start working once a rule answering it
 * is asserted.
 *
 * A query may compile to several services, of which this returns one; a caller wanting
 * every answer reads them through a MixedTypeRelation instead. See UserQuery() in
 * ui/query.h, which does exactly that.
 */
bool FindOrCompileService(
	Atom queryTermForm, TypedTuple const * queryActors, Service * service,
	index8 permutation[]);

#endif	// COMPILER_H
