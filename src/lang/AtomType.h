/**
 * The atom type determines how to interpret a atom.
 */

#ifndef ATOMTYPE_H
#define ATOMTYPE_H

#include "platform.h"


/**
 * Atom type IDs. Zero (null) indicates a missing value.
 */

#define AT_NAME					1		// a name (symbol) identified by a hash
#define AT_ID					2		// atom identified by an ifact
#define AT_INT					3		// signed integer
#define AT_FLOAT				4		// double-precision floating point number
#define AT_LETTER				5		// a letter of the English alphabet TODO: should be removed
#define AT_VARIABLE             6		// variable with a letter identifier
#define AT_PARAMETER			7		// a parameter in a service
#define AT_FORMULA				8		// a form plus its actors
#define AT_GENERATOR			9		// atom representing the * generator construct

#define N_ATOMTYPES				9


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
