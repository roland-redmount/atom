/**
 * The atom type determines how to interpret a atom.
 */

#ifndef ATOMTYPE_H
#define ATOMTYPE_H

#include "platform.h"


/**
 * Atom type IDs. Zero is invalid / none
 */

#define AT_NONE					0
#define AT_NAME					1		// a name (symbol) identified by a hash
#define AT_ID					2		// atom identified by an ifact
#define AT_UINT					3		// unsigned integer
#define AT_INT					4		// signed integer
#define AT_FLOAT				5		// double-precision floating point number
#define AT_LETTER				6		// a letter of the English alphabet
#define AT_VARIABLE             7		// variable with a letter identifier
#define AT_PARAMETER			8		// a parameter in a service
#define N_ATOMTYPES				8


/**
 * Get a atom type syntax string from its id.
 */
char const * GetAtomTypeName(byte type);

/**
 * Find a atom type id from its syntax string.
 * NOTE: this function does not handle extra whitespace.
 */
byte AtomTypeFromString(char const * syntax, size32 length);




#endif // ATOMTYPE_H
