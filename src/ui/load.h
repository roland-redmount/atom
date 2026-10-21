/**
 * CLAUDE: Loading facts from a text file into a relation.
 *
 * The first line of the file names the columns of the file. Each following line holds one
 * value in each column, written as an actor; see ParseActors(). The caller gives the term
 * form of the facts, and each role of the term form reads the column of the same name.
 * Columns not named by a role are not read. For example, the term form (child parent)
 * and a file of three lines
 *
 *   parent,age,child
 *   "Finwe",,"Feanor"
 *   "Finwe",,"Fingolfin"
 *
 * give two facts. The columns are separated by the first character of the first line that
 * cannot occur in a name, here ','. A first line with names and spaces only separates the
 * columns by spaces. A role name written twice in the term form, as in (spouse spouse), reads
 * two columns of that name.
 *
 * A line with an empty column read by a role is skipped. Blank lines are skipped, except the
 * first line.
 */

#ifndef LOAD_H
#define LOAD_H

#include "lang/Atom.h"


/*
 * CLAUDE: The outcome of loading a file, written by LoadRelationFile().
 */
typedef struct s_LoadReport {
	int result;				// one of the LOAD_* codes
	int assertResult;		// the ASSERT_* code from AssertFact(), for LOAD_ASSERT_FAILED
	uint32 lineNumber;		// the line where loading stopped, counting from 1
	index32 errorIndex;		// the character in that line where parsing failed, for LOAD_SYNTAX_ERROR
	size32 nColumns;		// the number of columns of that line, for LOAD_WRONG_COLUMN_COUNT
	size32 nHeaderColumns;	// the number of columns of the first line
	Atom missingRole;		// the role name with no column, for LOAD_MISSING_COLUMN
	size32 nAsserted;		// the number of facts asserted
	size32 nExisting;		// the number of facts that were already known
	size32 nSkipped;		// the number of lines skipped for an empty column
} LoadReport;

// CLAUDE: Result codes for LoadReport
#define LOAD_OK					1	// every line was loaded
#define LOAD_CANNOT_READ		2	// the file does not exist or cannot be read
#define LOAD_NO_HEADER			3	// the first line of the file is missing or blank
#define LOAD_LINE_TOO_LONG		4	// a line is longer than LOAD_MAX_LINE_LENGTH
#define LOAD_SYNTAX_ERROR		5	// a column is not a name in the first line, or not an actor
#define LOAD_WRONG_COLUMN_COUNT	6	// a line has a different number of columns than the first line
#define LOAD_ASSERT_FAILED		7	// AssertFact() rejected a fact
#define LOAD_MISSING_COLUMN		8	// the first line names no column for a role of the term form

#define LOAD_MAX_LINE_LENGTH	1024


/**
 * CLAUDE: Assert the facts of the given term form read from a text file. Loading stops at the
 * first line that is not valid, and the facts asserted from the lines before it are kept.
 * The term form remains owned by the caller.
 */
void LoadRelationFile(char const * filePath, Atom form, LoadReport * report);

/**
 * CLAUDE: Assert the facts of the given term form read from a C string holding the lines
 * of a text file; see LoadRelationFile().
 */
void LoadRelationText(char const * text, Atom form, LoadReport * report);


#endif	// LOAD_H
