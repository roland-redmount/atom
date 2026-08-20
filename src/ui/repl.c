/**
 * A read-eval-print loop for the atom system, run from the command line.
 *
 * Each line entered is a query: it is parsed to a term, answered by UserQuery(), and every
 * answer is printed as the query with its variables filled in. A line beginning with ':'
 * is a command instead, which is how everything that is not a query is asked for.
 *
 * The world a session starts with holds the core relations and the math services, and
 * nothing else, since there is no way yet to state a fact or a rule here.
 */

#include "kernel/kernel.h"
#include "lang/formula.h"
#include "lang/TermForm.h"
#include "library/MachineService.h"
#include "library/math.h"
#include "parser/FormulaBuilder.h"
#include "platform.h"
#include "ui/query.h"


#define LINE_BUFFER_SIZE	1024

#define PROMPT				"> "

// What executeLine() and executeCommand() report back to the loop in main()
#define REPL_CONTINUE		1
#define REPL_QUIT			2


/*
 * Indent to the left margin, which is where the prompt leaves the cursor. Everything
 * printed starts here, so that it lines up with what was typed after the prompt.
 */
static void printMargin(void)
{
	for(index32 i = 0; i < CStringLength(PROMPT); i++)
		PrintChar(' ');
}


/*
 * Print one line of text at the left margin.
 */
static void printLine(char const * text)
{
	printMargin();
	PrintCString(text);
	PrintChar('\n');
}


static void printBanner(void)
{
	printLine("atom v.0.1");
	PrintChar('\n');
	printLine("Enter a query, or :help for the commands.");
}


static void printHelp(void)
{
	printLine("Enter a query as a term, such as");
	printLine("    + 2 + 3 = s");
	printLine("and every fact answering it is printed. A variable is named by a single");
	printLine("letter, and _ stands for a variable whose name does not matter.");
	PrintChar('\n');
	printLine("Commands:");
	printLine("    :help            Print this text");
	printLine("    :quit, ctrl-D    End the session");
}


/*
 * Point at the character where a line went wrong. The terminal has already echoed the
 * line after the prompt, so the caret is indented by the width of the prompt to line up
 * under the character it names.
 */
static void printParseError(index32 errorPosition)
{
	printMargin();
	for(index32 i = 0; i < errorPosition; i++)
		PrintChar(' ');
	PrintCString("^ syntax error\n");
}


/*
 * Print a summary of the query results
 */
static void printQueryResultSummary(MixedTypeRelation const * resultRelations, size32 nTuples)
{
	size32 nServices = MixedTypeRelationNServices(resultRelations);
	printMargin();
	PrintF("%d facts from %d matching services\n", nTuples, nServices);
}


/*
 * Answer one query, printing every answer and how many there were. A line that is not a
 * query at all is reported here rather than being asked, since UserQuery() takes a term.
 */
static void executeQuery(char const * line)
{
	index32 errorPosition;
	Atom query = ParseFormula(line, &errorPosition);
	if(!query.hash) {
		printParseError(errorPosition);
		return;
	}

	Atom form = FormulaGetForm(query);
	if(!IsTermForm(form)) {
		printLine("A query must be a single term.");
		ReleaseFormula(query);
		return;
	}

	MixedTypeRelation * resultRelations = UserQuery(query);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(resultRelations)) {
		printMargin();
		PrintFormActorsAsFormula(form, MixedTypeRelationPeekTuple(resultRelations));
		PrintChar('\n');
		nTuples++;
	}
	// the relation counts the services it read, so it is read out before being freed
	printQueryResultSummary(resultRelations, nTuples);
	FreeMixedTypeRelation(resultRelations);
	ReleaseFormula(query);
}


/*
 * Match the command a line begins with. Returns the command's argument, which is whatever
 * follows the command word with the space between them dropped, or 0 if the line begins
 * with some other command. A command taking no argument is matched with an empty argument.
 */
static char const * matchCommand(char const * line, char const * command)
{
	index32 i = 0;
	while(command[i] && (line[i] == command[i]))
		i++;
	if(command[i])
		return 0;
	// the word the line begins with has to end where the command does, so that
	// ":quitting" is not read as ":quit"
	if(line[i] && !IsSpaceChar(line[i]))
		return 0;

	while(IsSpaceChar(line[i]))
		i++;
	return line + i;
}


/*
 * Execute a command line, which begins with ':'. This is where asserting a fact and
 * asserting a rule will be dispatched from.
 */
static int executeCommand(char const * line)
{
	if(matchCommand(line, ":quit"))
		return REPL_QUIT;

	if(matchCommand(line, ":help")) {
		printHelp();
		return REPL_CONTINUE;
	}

	printLine("Unknown command. Enter :help for the commands.");
	return REPL_CONTINUE;
}


static int executeLine(char const * line)
{
	char const * firstCharacter = line;
	while(IsSpaceChar(*firstCharacter))
		firstCharacter++;

	if(!*firstCharacter)
		return REPL_CONTINUE;

	if(*firstCharacter == ':')
		return executeCommand(firstCharacter);

	// The query is parsed from the whole line, so that the position of a syntax error
	// counts from where the line began rather than from its first word.
	executeQuery(line);
	return REPL_CONTINUE;
}


int main(int argc, char * argv[])
{
	KernelInitialize();
	MathSetup();
	printBanner();

	char line[LINE_BUFFER_SIZE];
	bool isRunning = true;
	while(isRunning) {
		PrintCString(PROMPT);
		switch(ReadLine(line, LINE_BUFFER_SIZE)) {
		case READLINE_OK:
			isRunning = (executeLine(line) == REPL_CONTINUE);
			break;

		case READLINE_TOO_LONG:
			printMargin();
			PrintF("A line may hold at most %u characters.\n", LINE_BUFFER_SIZE - 1);
			break;

		case READLINE_END:
			// end of input, from a redirected file or Ctrl-D at the terminal.
			// The prompt was printed and nothing was typed after it.
			PrintChar('\n');
			isRunning = false;
			break;
		}
	}

	FreeMachineServices();
	KernelShutdown();
	return 0;
}
