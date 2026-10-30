/**
 * A session takes text strings (lines) that may be queries or commands,
 * evaluates them and prints responses to stdout.
 * A line containing a formula is a query. A line beginning with ':' is a command instead.
 * 
 * The session also lets the user refer to ID atoms using tags of the form @tag.
 * This feature is not part of the atom language.
 */


#ifndef SESSION_H
#define SESSION_H

#include "platform.h"


// The prompt used prior to user input
#define SESSION_PROMPT		"> "

// The prompt used when continuing input over multiple lines.
// This prompt must have the same length as SESSION_PROMPT to correctly align an error caret.
#define SESSION_CONTINUATION_PROMPT	"  "

// Prefix used for a status line; see SessionPrintStatusPrefix()
#define SESSION_STATUS_PREFIX	">> "

// Return codes for SessionExecuteLine()
#define SESSION_CONTINUE	1
#define SESSION_QUIT		2
#define SESSION_INCOMPLETE	3


/**
 * Print the text a session opens with, naming the command that lists the commands.
 */
void SessionPrintBanner(void);

/**
 * Begin a status line, such as a summary or an error message, so that the status
 * line is told apart from a query result. A front end printing a message of its own uses
 * this too.
 */
void SessionPrintStatusPrefix(void);

/**
 * Execute one one line of input and print results. Returns SESSION_QUIT once the
 * session has been asked to end, and SESSION_CONTINUE while it has not.
 */
int SessionExecuteLine(char const * line);


#endif	// SESSION_H
