

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
 * Find the service answering a query, or compile a service if none is registered yet.
 * Copies the resulting Service to *service. Returns true if a service was found.
 * The permutation array receives the argument permutation of the match, as for DispatchQuery().
 *
 * NOTE: a query may compile to several services, of which this returns one.
 * To obtain all services, see UserQuery().
 */
bool FindOrCompileService(FormulaView query, Service * service, index8 permutation[]);

#endif	// COMPILER_H
