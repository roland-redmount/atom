

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/ServiceRegistry.h"

/**
 * Compile a query and write the generated service record
 * into the given recod. Returns true if compilation succeeded.
 */
bool CompileService(Atom queryTerm, ServiceRecord * record);


#endif	// COMPILER_H
