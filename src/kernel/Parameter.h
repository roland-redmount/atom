/**
 * An AT_PARAMETER aton is used in service signatures to mark a position
 * in the actor list where a parameter is a expected. Parameters are
 * different from variables: while a variable indicates "any atom"
 * and has no particular input/output direction, an parameter
 * indicates one specific atom, although its identity is unknown.
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
 * Service parameters can be input, output, or bidirectional.
 * In syntax, we denote input parameters by @, output by $.
 * (Maybe < and > are more intuitive after all? Output feel
 *  like going right-to-left so should be >, consistent with
 *  Unix pipes.)
 * Machine level programs such as table services may have
 * parameters than allow both input and output; these can be used 
 * to represent multiple input/output combination compactly.
 */
#define PARAMETER_IN		1
#define PARAMETER_OUT		2
#define PARAMETER_IN_OUT	3

/**
 * Create an parameter. For untyped parameters, set type to 0.
 */
Atom CreateParameter(byte io, byte type);

bool IsParameter(TypedAtom atom);

byte ParameterGetType(Atom parameter);

byte ParameterGetIO(Atom parameter);


/**
 * Comparison function for parameters, used to compare
 * parameter lists; see ServiceRegistry.c
 * 
 * The type AT_NONE  and io mode PARAMETER_IN_OUT are treated as
 * wildcards, matching any other value.  Therefore, distinct parameter
 * atoms can compare equal by this function.
 */
int8 CompareParameters(Atom parameter1, Atom parameter2);

void PrintParameter(Atom parameter);


#endif	// PARAMETER_H
