

#ifndef	COMPILER_H
#define COMPILER_H

#include "kernel/service.h"
#include "lang/Formula.h"

/**
 * Attempt to compile a query. If successful, registers the generated service,
 * and returns it.
 * 
 * TODO: this should probably take a term form + a tuple; we don't need to
 * store a formula.
 */
Service const * CompileService(Formula const * queryTerm);


#endif	// COMPILER_H
