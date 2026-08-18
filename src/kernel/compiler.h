

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"

// Upper bound on the number of services one query may compile to
#define MAX_COMPILED_SERVICES	8

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

/**
 * Generate a parameters tuple from an actors tuple, such that each non-variable atom
 * in the actors tuple corresponds to an input parameter (with type preserved),
 * and each variable yields an output parameter. The output parameter types are
 * unknown and must be discovered later by matching against services.
 * The generated parameter numbers are always equal to the tuple index (1-based).
 * The two tuples must have the same number of atoms.
 *
 * The result is the query generalized to its type: the term form together with the
 * direction and input type of each parameter is what determines the services a query
 * compiles to, and hence whether it has been compiled before; see UserQuery().
 *
 * NOTE: the parameters tuple could be an Atom[] as the type is constant, but this
 * currently doesn't fit with compileQuery() and downstream functions.
 */
void GetQueryParameters(TypedTuple const * actors, TypedTuple * parameters);


#endif	// COMPILER_H
