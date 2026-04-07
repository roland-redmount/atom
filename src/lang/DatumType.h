/**
 * The datum type determines how to interpret a datum,
 * and is used to determine how to dispatch calls to bytecode programs
 */

#ifndef DATUMTYPE_H
#define DATUMTYPE_H

#include "platform.h"


/**
 * Datum type IDs. Zero is invalid / none
 */

#define DT_NONE					0
#define DT_NAME					1		// a name (symbol) identified by a hash
#define DT_ID					2		// atom identified by an ifact

#define DT_UINT					3		// unsigned integer
#define DT_INT					4		// signed integer
#define DT_FLOAT32				5		// double-precision floating point number
#define DT_FLOAT64				6		// double-precision floating point number

#define DT_LETTER				7		// a letter of the English alphabet
#define DT_VARIABLE             8		// variable with a letter identifier

#define DT_INSTRUCTION			9		// a bytecode instruction
#define DT_PARAMETER			10		// a parameter in a bytecode service
#define DT_CONTEXT				11		// a VM execution context

#define N_DATUMTYPES			11


/**
 * Get a datum type syntax string from its id.
 */
char const * GetDatumTypeName(byte typeId);

/**
 * Find a datum type id from its syntax string.
 * NOTE: this function does not handle extra whitespace.
 */
byte DatumTypeIdFromString(char const * syntax, size32 length);




#endif // DATUMTYPES_H
