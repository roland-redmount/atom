/**
 * A session answers the lines a user enters, whatever front end reads them.
 *
 * Each line entered is a query: it is parsed to a term, answered by UserQuery(), and every
 * answer is printed as the query with its variables filled in. A line beginning with ':'
 * is a command instead, which is how everything that is not a query is asked for.
 *
 * The world a session starts with holds the core relations and the math services, and
 * whatever the session states with :assert.
 *
 * A session prints to the output stream and reads nothing, so a front end is left with
 * obtaining a line and deciding when to stop. See ui/repl.c for the command line one.
 */

#ifndef SESSION_H
#define SESSION_H

#include "platform.h"


/**
 * What a line is entered after. A front end prints this before reading a line, so that
 * a session lines its own output up with what was typed; see SessionPrintMargin().
 */
#define SESSION_PROMPT		"> "

// What SessionExecuteLine() reports back to the front end that called it
#define SESSION_CONTINUE	1
#define SESSION_QUIT		2


/**
 * Print the text a session opens with, naming the command that lists the commands.
 */
void SessionPrintBanner(void);


/**
 * Indent to the left margin, which is where the prompt leaves the cursor. Everything a
 * session prints starts here, so that it lines up with what was typed after the prompt.
 * A front end printing a message of its own uses this to line that message up too.
 */
void SessionPrintMargin(void);


/**
 * Answer one line, printing whatever the line asks for. Returns SESSION_QUIT once the
 * session has been asked to end, and SESSION_CONTINUE while it has not.
 */
int SessionExecuteLine(char const * line);


#endif	// SESSION_H
