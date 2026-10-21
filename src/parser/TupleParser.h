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

/**
 * A column is a span of a line, as found by SplitColumns(). Any spaces around
 * the column are not part of the span.
 */
typedef struct s_Column {
	index32 start;		// the index in the line where the column begins
	size32 length;
} Column;

/**
 * Split a C string into columns separated by the separator character, and return the
 * number of columns. At most maxColumns columns are written to the columns array.
 * Each separator character ends a column, so that "a,,b" has three columns, the second
 * one empty. A separator character inside a string "..." does not end a column.
 */
size32 SplitColumns(char const * cString, char separator, Column columns[], size32 maxColumns);

/**
 * CLAUDE: Parse the column of a C string to a single actor; see ParseActors(). An empty
 * column holds no actor, and sets isEmpty. The caller obtains a reference to the actor.
 *
 * Returns false for a column that holds anything other than one actor, and writes the index
 * of the character where parsing failed to errorIndex. The index counts from the start of
 * the C string, not from the start of the column.
 */
bool ParseColumnActor(
	char const * cString, Column column, TypedAtom * actor, bool * isEmpty, index32 * errorIndex);


#endif	// TUPLEPARSER_H
