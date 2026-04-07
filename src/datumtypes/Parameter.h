/**
 * A AT_PARAMETER aton is used in service signatures to mark a position
 * in the actor list where a parameter is a expected.
 * A parameter is either "input" or "output" and may optionally have a atom type.
 */

#ifndef	PARAMETER_H
#define	PARAMETER_H 

#include "lang/TypedAtom.h"
#include "lang/Atom.h"

#define PARAMETER_IN	1
#define PARAMETER_OUT	2

/**
 * Create an parameter. For untyped parameters, set type to 0.
 */
TypedAtom CreateParameter(byte io, byte type);

bool IsParameter(TypedAtom a);

void PrintParameter(TypedAtom parameter);


#endif	// PARAMETER_H
