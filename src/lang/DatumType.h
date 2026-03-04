/**
 * The datum type determines how to interpret a datum,
 * and is used to determine how to dispatch calls to bytecode programs
 */

#ifndef DATUMTYPE_H
#define DATUMTYPE_H

#include "platform.h"


/**
 * Datum type IDs. Zero is invalid.
 */

#define DT_UNKNOWN				1		// unique IDs, including the nil atom
#define DT_DATUMTYPE			2

#define DT_UINT					3		// unsigned integer
#define DT_INT					4		// signed integer
#define DT_FLOAT32				5		// double-precision floating point number
#define DT_FLOAT64				6		// double-precision floating point number

#define DT_LETTER				7		// a letter of the English alphabet
#define DT_VARIABLE             8		// variable with a letter identifier
#define DT_NAME					9		// a name (symbol) identified by a hash
#define DT_ID					10		// atom identified by an ifact

#define DT_INSTRUCTION			11		// a bytecode instruction
#define DT_PARAMETER			12		// a parameter in a bytecode service

#define N_DATUMTYPES			12


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
