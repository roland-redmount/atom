

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/ServiceRegistry.h"
#include "lang/Formula.h"

/**
 * Attempt to compile a query, registering every generated service and writing
 * its record to the records array. Returns the number of records written.
 * The service(s) to be compiled must not already exist before this call.
 *
 * A query may compile to more than one service. Where the query leaves an
 * output parameter untyped, its type is unconstrained, and each relation table
 * registered for the term form yields a separately typed service. A query over
 * a list, say, compiles to one service per element type. Callers enumerating
 * results must therefore iterate over all returned records.
 *
 * TODO: this should probably take a term form + a tuple; we don't need to
 * store a formula.
 */
size8 CompileService(Formula const * queryTerm, ServiceRecord records[], size8 maxRecords);


#endif	// COMPILER_H
