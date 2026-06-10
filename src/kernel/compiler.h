

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/ServiceRegistry.h"

/**
 * Attempt to compile a query. If successful, registers the generated service,
 * writes to the given ServiceRedord, and returns true.
 * 
 * TODO: this should probably take a term form + a tuple; we don't need to
 * store a formula.
 */
bool CompileService(Atom queryTerm, ServiceRecord * record);


#endif	// COMPILER_H
