/**
 * A AT_PARAMETER aton is used in service signatures to mark a position
 * in the actor list where a parameter is a expected.
 */

#ifndef	PARAMETER_H
#define	PARAMETER_H 

#include "lang/TypedAtom.h"
#include "lang/Atom.h"

/**
 * For bytecode programs, each parameter is either input or output.
 * Machine level program such as table services may have
 * parameters than allow both input and output; these can be used 
 * to represent multiple input/output combination compactly.
 */
#define PARAMETER_IN		1
#define PARAMETER_OUT		2
#define PARAMETER_IN_OUT	3

/**
 * Create an parameter. For untyped parameters, set type to 0.
 */
TypedAtom CreateParameter(byte io, byte type);

bool IsParameter(TypedAtom a);

/**
 * Comparison function for parameters, used to compare
 * parameter lists; see ServiceRegistry.c
 * 
 * The type AT_NONE  and io mode PARAMETER_IN_OUT are treated as
 * wildcards, matching any other value.  Therefore, distinct parameter
 * atoms can compare equal by this function.
 */
int8 CompareParameters(Atom parameter1, Atom parameter2);

void PrintParameter(TypedAtom parameter);


#endif	// PARAMETER_H
