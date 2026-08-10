

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"

/**
 * Attempt to compile a query, registering every generated service and writing
 * a copy to the services[] array. Returns the number of services written.
 * The service(s) to be compiled must not already exist before this call.
 *
 * A query may compile to more than one service: this occurs if at any point
 * in the compilation multiple services with same form but distinct types
 * are matched. For example, the query (list <my_list> position p element e)
 * compiles to one service per element type. Callers enumerating
 * results must therefore iterate over all returned services.
 *
 * TODO: this should probably take a term form + a tuple; we don't need to
 * store a formula.
 */
size8 CompileQuery(Formula const * queryTerm, Service services[], size8 maxServices);


#endif	// COMPILER_H
