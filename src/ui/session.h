/**
 * A session takes text strings (lines) that may be queries or commands,
 * evaluates them and prints responses to stdout.
 * A line containing a formula is a query. A line beginning with ':' is a command instead.
 * 
 * ID atoms received in query results are printed as @number, and the session lets the user
 * refer to those ID atoms by @number in following input. This number is kept by the session only,
 * and is not part of the language; see expandIDNumbers() in ui/session.c.
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
