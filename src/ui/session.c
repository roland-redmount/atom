/**
 * A session answering the lines a front end reads. See ui/session.h.
 */

#include "lang/formula.h"
#include "ui/assert.h"
#include "lang/TermForm.h"
#include "library/string.h"
#include "parser/FormulaBuilder.h"
#include "platform.h"
#include "ui/query.h"
#include "ui/session.h"


void SessionPrintMargin(void)
{
	for(index32 i = 0; i < CStringLength(SESSION_PROMPT); i++)
		PrintChar(' ');
}


/*
 * Print one line of text at the left margin.
 */
static void printLine(char const * text)
{
	SessionPrintMargin();
	PrintCString(text);
	PrintChar('\n');
}


void SessionPrintBanner(void)
{
	PrintChar('\n');
	printLine("atom v.0.1");
	PrintChar('\n');
	printLine("Enter a query, or :help for the commands.");
	PrintChar('\n');
}


static void printHelp(void)
{
	PrintChar('\n');
	printLine("Enter a query as a term, such as");
	printLine("  + 2 + 3 = s");
	printLine("to view every matching fact in the knowledgebase.");
	printLine("Variables are single letters and _ is the anonymous variable.");
	PrintChar('\n');
	printLine("Commands:");
	printLine("  :assert <term>      Assert a fact. The term must not contain variables.");
	printLine("  :assert <clause>    Assert a rule. The clause must have at least two terms");
	printLine("                      and contain at least one variable.");
	printLine("  :help               Print this text.");
	printLine("  :quit, ctrl-D       End the session.");
	PrintChar('\n');
}


/*
 * Point at the character where a line went wrong. The terminal has already echoed the
 * line after the prompt, so the caret is indented by the width of the prompt to line up
 * under the character it names.
 */
static void printParseError(index32 errorPosition)
{
	SessionPrintMargin();
	for(index32 i = 0; i < errorPosition; i++)
		PrintChar(' ');
	PrintCString("^ syntax error\n");
}


/*
 * Print a summary of the query results
 */
static void printQueryResultSummary(MixedTypeRelation const * mixedTypeRelation, size32 nTuples)
{
	size32 nServices = MixedTypeRelationNServices(mixedTypeRelation);
	SessionPrintMargin();
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
	FormulaView queryView = FormulaGetView(query);
	if(!IsTermForm(queryView.form)) {
		printLine("A query must be a single term.");
		ReleaseFormula(query);
		return;
	}

	MixedTypeRelation * resultRelations = UserQuery(queryView);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(resultRelations)) {
		SessionPrintMargin();
		PrintFormActorsAsFormula(queryView.form, MixedTypeRelationPeekTuple(resultRelations));
		PrintChar('\n');
		nTuples++;
	}
	// the relation counts the services it read, so it is read out before being freed
	printQueryResultSummary(resultRelations, nTuples);
	FreeMixedTypeRelation(resultRelations);
	ReleaseFormula(query);
}


/*
 * Report what asserting a formula came to, naming the rule broken by one that could be
 * neither a fact nor a rule.
 */
static void printAssertResult(int result)
{
	switch(result) {
	case ASSERT_OK:
		printLine("Asserted.");
		break;

	case ASSERT_EXISTED:
		printLine("Already known.");
		break;

	case ASSERT_FAIL:
		printLine("Contradicts the knowledgebase.");
		break;

	case ASSERT_TERM_VARIABLE:
		printLine("A fact may not contain a variable.");
		break;

	case ASSERT_CLAUSE_NO_VARIABLE:
		printLine("A rule must contain at least one variable.");
		break;

	case ASSERT_CLAUSE_ONE_TERM:
		printLine("A rule must contain at least two terms.");
		break;

	case ASSERT_NOT_CLAUSE:
		printLine("Only a fact or a rule can be asserted.");
		break;

	default:
		ASSERT(false)
		break;
	}
}


/*
 * Assert one formula, which is the text following the :assert command. That text begins
 * part way into the line the user typed, so its position there is what puts the caret of a
 * syntax error under the character it names.
 */
static void executeAssert(char const * formulaText, index32 linePosition)
{
	if(!*formulaText) {
		printLine(":assert requies a term or a clause.");
		return;
	}

	index32 errorPosition;
	Atom formula = ParseFormula(formulaText, &errorPosition);
	if(!formula.hash) {
		printParseError(linePosition + errorPosition);
		return;
	}

	printAssertResult(AssertFormula(formula));
	ReleaseFormula(formula);
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
 * Execute a command, which is the word beginning with ':' that the given command text
 * starts with. The whole line is passed as well, since a command argument is reported at
 * its position in the line the user typed rather than in the argument itself.
 */
static int executeCommand(char const * line, char const * commandText)
{
	if(matchCommand(commandText, ":quit"))
		return SESSION_QUIT;

	if(matchCommand(commandText, ":help")) {
		printHelp();
		return SESSION_CONTINUE;
	}

	char const * formulaText = matchCommand(commandText, ":assert");
	if(formulaText) {
		executeAssert(formulaText, formulaText - line);
		return SESSION_CONTINUE;
	}

	printLine("Unknown command. Enter :help for the commands.");
	return SESSION_CONTINUE;
}


int SessionExecuteLine(char const * line)
{
	char const * firstCharacter = line;
	while(IsSpaceChar(*firstCharacter))
		firstCharacter++;

	if(!*firstCharacter)
		return SESSION_CONTINUE;

	if(*firstCharacter == ':')
		return executeCommand(line, firstCharacter);

	// The query is parsed from the whole line, so that the position of a syntax error
	// counts from where the line began rather than from its first word.
	executeQuery(line);
	return SESSION_CONTINUE;
}
