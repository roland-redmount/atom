/**
 * CLAUDE: Loading facts from a text file into a relation.
 *
 * The first line of the file lists role names, which define a term form; see
 * ParseTermForm(). Each following line lists the actors of one fact, in the same order
 * as the role names; see ParseActors(). For example, a file of three lines
 *
 *   parent child
 *   "Finwe" "Feanor"
 *   "Finwe" "Fingolfin"
 *
 * holds two facts of the term form (parent child). Blank lines are skipped, and a tab
 * counts as a space.
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
	size8 nActors;			// the number of actors on that line, for LOAD_WRONG_ARITY
	Atom form;				// the term form of the first line, or the zero atom
	size32 nAsserted;		// the number of facts asserted
	size32 nExisting;		// the number of facts that were already known
} LoadReport;

// CLAUDE: Result codes for LoadReport
#define LOAD_OK					1	// every line was loaded
#define LOAD_CANNOT_READ		2	// the file does not exist or cannot be read
#define LOAD_NO_FORM			3	// the file holds no line of role names
#define LOAD_LINE_TOO_LONG		4	// a line is longer than LOAD_MAX_LINE_LENGTH
#define LOAD_SYNTAX_ERROR		5	// a line is not a list of role names or actors
#define LOAD_WRONG_ARITY		6	// a line has a different number of actors than the form has roles
#define LOAD_ASSERT_FAILED		7	// AssertFact() rejected a fact

#define LOAD_MAX_LINE_LENGTH	1024


/**
 * CLAUDE: Assert the facts of a text file. Loading stops at the first line that is not
 * valid, and the facts asserted from the lines before it are kept. The caller must
 * release report->form with IFactRelease() unless report->form is the zero atom.
 */
void LoadRelationFile(char const * filePath, LoadReport * report);

/**
 * CLAUDE: Assert the facts of a C string holding the lines of a text file;
 * see LoadRelationFile().
 */
void LoadRelationText(char const * text, LoadReport * report);


#endif	// LOAD_H
