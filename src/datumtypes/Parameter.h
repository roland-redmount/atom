/**
 * Parameters are used in service signatures.
 * They are either "input" or "output" and may optionally have a type.
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
Atom CreateParameter(char name, byte io, byte type);

bool IsParameter(Atom a);


char GetParameterName(Atom parameter);

void PrintParameter(Atom parameter);


#endif	// PARAMETER_H
