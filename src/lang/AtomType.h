/**
 * The atom type determines how to interpret a atom,
 * and is used to determine how to dispatch calls to bytecode programs
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
#define AT_FLOAT32				5		// double-precision floating point number
#define AT_FLOAT64				6		// double-precision floating point number

#define AT_LETTER				7		// a letter of the English alphabet
#define AT_VARIABLE             8		// variable with a letter identifier

#define AT_INSTRUCTION			9		// a bytecode instruction
#define AT_PARAMETER			10		// a parameter in a bytecode service
#define AT_CONTEXT				11		// Used for registers storing a VM execution context
#define AT_SERVICE				12		// a service record

#define N_ATOMTYPES			11


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
