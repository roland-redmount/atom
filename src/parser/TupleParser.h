/**
 * Parser for a tuple of actors.
 */

#ifndef TUPLEPARSER_H
#define TUPLEPARSER_H

#include "lang/TypedAtom.h"


/**
 * Parse a C string of actors separated by whitespace, such as "\"Finwe\" 42 'A".
 * An actor is a string, a number, a letter or an ID atom written by its hash.
 * At most maxActors actors are read into the actors array, in the order written,
 * and their number is written to nActors. The caller obtains a reference to each actor.
 *
 * Returns false for a string that is not such a list of actors, and writes the index
 * of the character where parsing failed to errorIndex. No actor is kept in that case.
 */
bool ParseActors(
	char const * cString, TypedAtom actors[], size8 maxActors, size8 * nActors, index32 * errorIndex);


#endif	// TUPLEPARSER_H
