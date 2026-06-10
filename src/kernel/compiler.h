

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/ServiceRegistry.h"

/**
 * Attempt to compile a query. If successful, registers the generated service,
 * writes to the given ServiceRedord, and returns true.
 */
bool CompileService(Atom queryTerm, ServiceRecord * record);


#endif	// COMPILER_H
