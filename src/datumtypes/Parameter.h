/**
 * A DT_PARAMETER aton is used in service signatures to mark a position
 * in the actor list where a parameter is a expected.
 * A parameter is either "input" or "output" and may optionally have a datum type.
 */

#ifndef	PARAMETER_H
#define	PARAMETER_H 

#include "lang/Atom.h"
#include "lang/Datum.h"

#define PARAMETER_IN	1
#define PARAMETER_OUT	2

/**
 * Create an parameter. For untyped parameters, set type to 0.
 */
Atom CreateParameter(byte io, byte type);

bool IsParameter(Atom a);

void PrintParameter(Atom parameter);


#endif	// PARAMETER_H
