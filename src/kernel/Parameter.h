/**
 * An AT_PARAMETER atom is used in service signatures to mark a position
 * in the actor list where a parameter is a expected. Parameters are
 * different from variables: while a variable indicates "any atom"
 * and has no particular input/output direction, an parameter
 * indicates one specific atom, although its identity is unknown.
 * Parameters are identified within a formula by a unique number.
 * 
 * Output parameters may superfically seem similar to variables as we can
 * use variables in place of "outputs" in queries, but they are not
 * the same: a variable can during compilation be mapped to either an
 * input or output parameter, depending on the service signatures.
 */

#ifndef	PARAMETER_H
#define	PARAMETER_H 

#include "lang/TypedAtom.h"
#include "lang/Atom.h"

/**
 * Service parameters can be input, output.
 * In syntax, we denote input parameters by @, output by $.
 * (Maybe < and > are more intuitive after all? Output feel
 *  like going right-to-left so should be >, consistent with
 *  Unix pipes.)
 * 
 * PARAMETER_IN_OUT can be used to represent input/output combinations
 * compactly for services that can act on either inputs or outputs,
 * such as table lookup with / without variables.
 */
#define PARAMETER_IN		1
#define PARAMETER_OUT		2


void PrintParameter(Atom parameter);


#endif	// PARAMETER_H
