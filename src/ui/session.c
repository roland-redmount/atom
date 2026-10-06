/**
 * A session answering the lines a front end reads. See ui/session.h.
 */

#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/Parameter.h"
#include "lang/formula.h"
#include "ui/assert.h"
#include "ui/load.h"
#include "lang/TermForm.h"
#include "library/string.h"
#include "parser/Characters.h"
#include "parser/FormulaBuilder.h"
#include "parser/StringBuffer.h"
#include "parser/Tokenizer.h"
#include "platform.h"
#include "ui/query.h"
#include "ui/session.h"
#include "util/ResizingArray.h"
#include "memory/allocator.h"
#include "memory/paging.h"


/*
 * An IDExpansion records the position in the line input of a user-numbered ID atom
 * and its length.
 */
typedef struct s_IDExpansion {
	index32 linePosition;
	size32 inputLength;		// including the @ character
} IDExpansion;

typedef struct s_SessionState {
	/*
	 * The session numbers each ID atom in the order received from queries, and prints the
	 * ID atom as @number. The user may then refer to the same ID atom by entering @number
	 * in actions and queries. The session holds a reference to each numbered ID atom,
	 * so that the number stays valid for the rest of the session. The ID atom numbered n is the
	 * element n - 1 of numberedIDs.
	 */
	ResizingArray numberedIDs;

	// The line input expanded so that each ID atom number is replaced by the hash of the ID atom.
	StringBuffer expandedLine;
	// Each IDExpansion in idExpansions records one replacement, in the order of the line.
	ResizingArray idExpansions;
} SessionState;

/* Session state, persisted in STATE_KEY_SESSION. SessionExecuteLine() sets
   sessionState, and creates the module state on the first line of a new world. */
static SessionState * sessionState = 0;


static void initializeSession(void)
{
	sessionState = Allocate(sizeof(SessionState));
	SetPersistentState(STATE_KEY_SESSION, sessionState);
	CreateResizingArray(&sessionState->numberedIDs, sizeof(Atom), 16);
	CreateResizingArray(&sessionState->idExpansions, sizeof(IDExpansion), 4);
	StringBufferInit(&sessionState->expandedLine);
}


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
	printLine("Enter a query as a term or conjunction, such as");
	printLine("  + 2 + 3 = s    or    foo x bar y & foo y bar z");
	printLine("to view every matching fact in the knowledgebase.");
	printLine("Variables are single letters and _ is the anonymous variable.");
	printLine("An ID atom is printed as @1, @2 and so on, and can be entered the same way.");
	PrintChar('\n');
	printLine("Commands:");
	printLine("  :assert <term>        Assert a fact. The term must not contain variables.");
	printLine("  :assert <clause>      Assert a rule. The clause must have at least two terms");
	printLine("                        and contain at least one variable.");
	printLine("  :assert <term with *> Assert an ifact rule, such as (circle * radius r).");
	printLine("  :inspect <formula>    Print the service(s) that the formula dispatches to");
	printLine("  :load <file>          Assert the facts in a text file. The first line lists the");
	printLine("                        role names, and each following line the actors of one fact.");
	printLine("  :retract <term>       Retract a fact. The term must not contain variables.");
	printLine("  :help                 Print this text.");
	printLine("  :quit, ctrl-D         End the session.");
	PrintChar('\n');
}


/*
 * Return the number of an ID atom, numbering the ID atom first if it has no number.
 */
static index32 numberID(Atom id)
{
	for(index32 i = 0; i < sessionState->numberedIDs.nElements; i++) {
		Atom const * numberedID = ResizingArrayGetElement(&sessionState->numberedIDs, i);
		if(SameAtoms(*numberedID, id))
			return i + 1;
	}
	IFactAcquire(id);
	ResizingArrayAppend(&sessionState->numberedIDs, &id);
	return sessionState->numberedIDs.nElements;
}


/*
 * Print an ID atom as @ and its number; see IFactSetPrinter().
 */
static void printNumberedID(Atom id)
{
	PrintF("@%u", numberID(id));
}


/*
 * Map a position in the expanded line to the position in the line entered. A position
 * inside an expanded hash maps to the @ of the ID atom number it replaced.
 */
static index32 findLinePosition(index32 expandedPosition)
{
	// growth is how much longer the expanded line is than the line entered, over the expansions passed
	size32 growth = 0;
	for(index32 i = 0; i < sessionState->idExpansions.nElements; i++) {
		IDExpansion const * expansion = ResizingArrayGetElement(&sessionState->idExpansions, i);
		index32 expandedStart = expansion->linePosition + growth;
		if(expandedPosition < expandedStart)
			break;
		if(expandedPosition < expandedStart + (1 + ID_HASH_LENGTH))
			return expansion->linePosition;
		growth += (1 + ID_HASH_LENGTH) - expansion->inputLength;
	}
	return expandedPosition - growth;
}


/*
 * Point at the character where a line went wrong. The terminal has already echoed the
 * line after the prompt, so the caret is indented by the width of the prompt to line up
 * under the character it names.
 */
static void printParseError(index32 errorPosition)
{
	// errorPosition counts in the expanded line; see expandIDNumbers()
	index32 linePosition = findLinePosition(errorPosition);
	SessionPrintMargin();
	for(index32 i = 0; i < linePosition; i++)
		PrintChar(' ');
	PrintCString("^ syntax error\n");
}


/*
 *  Point at an invalid ID atom in the input line, like printParseError() does.
 */
static void printUnknownIDNumber(index32 linePosition)
{
	SessionPrintMargin();
	for(index32 i = 0; i < linePosition; i++)
		PrintChar(' ');
	PrintCString("^ no ID atom has this number\n");
}


/*
 * Copy an input line to expandedLine, replacing each user ID number by the full hash of
 * the ID atom, written as @ followed by ID_HASH_LENGTH hex digits; see TOKEN_ID. An ID atom number
 * is @ and a decimal number, at the start of a word and outside of a string. Other text is
 * copied as is, and left for ParseFormula() to judge. Returns false, having printed an
 * error, if an ID atom number names no numbered ID atom.
 */
static bool expandIDNumbers(char const * line)
{
	StringBufferReset(&sessionState->expandedLine);
	ResizingArrayReset(&sessionState->idExpansions);
	bool isInString = false;
	index32 i = 0;
	while(line[i]) {
		if(line[i] == '"')
			isInString = !isInString;

		bool isWordStart = (i == 0) || !IsNameChar(line[i - 1]);
		if((line[i] != '@') || isInString || !isWordStart) {
			StringBufferPush(&sessionState->expandedLine, line[i++]);
			continue;
		}

		// a hash has ID_HASH_LENGTH digits, so fewer decimal digits make an ID atom number
		size32 nDigits = 0;
		bool isDecimal = true;
		while(IsHexDigitChar(line[i + 1 + nDigits])) {
			isDecimal = isDecimal && IsDigitChar(line[i + 1 + nDigits]);
			nDigits++;
		}
		if((nDigits == 0) || (nDigits >= ID_HASH_LENGTH) || !isDecimal) {
			StringBufferPush(&sessionState->expandedLine, line[i++]);
			continue;
		}

		int64 number = StringToInt64(line + i + 1, nDigits);
		if((number == 0) || (number > sessionState->numberedIDs.nElements)) {
			printUnknownIDNumber(i);
			return false;
		}
		Atom const * id = ResizingArrayGetElement(&sessionState->numberedIDs, number - 1);
		char hashString[ID_HASH_LENGTH + 2];
		FormatString(hashString, sizeof(hashString), "@%016llx", (unsigned long long) id->hash);
		for(index32 j = 0; hashString[j]; j++)
			StringBufferPush(&sessionState->expandedLine, hashString[j]);

		IDExpansion expansion = {
			.linePosition = i,
			.inputLength = 1 + nDigits,
		};
		ResizingArrayAppend(&sessionState->idExpansions, &expansion);
		i += 1 + nDigits;
	}
	StringBufferPush(&sessionState->expandedLine, 0);
	return true;
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
 * Print a summary of the services a query dispatches to
 */
static void printInspectSummary(size32 nServices)
{
	if(nServices == 0) {
		printLine("No matching service. Execute a query to compile a service.");
		return;
	}
	SessionPrintMargin();
	PrintF("%d matching services\n", nServices);
}


/**
 * Submit one query and print each resulting fact and the total number of facts.
 */
static void executeQuery(char const * line)
{
	index32 errorPosition;
	FormOrdering ordering;
	Atom query = ParseFormula(line, &ordering, &errorPosition);
	if(!query.hash) {
		printParseError(errorPosition);
		return;
	}
	FormulaView queryView = FormulaGetView(query);
	if(!IsRelationForm(queryView.form)) {
		printLine("A query must be a term or a conjunction of terms.");
		ReleaseFormula(query);
		return;
	}

	MixedTypeRelation * resultRelations = UserQuery(queryView);
	size32 nTuples = 0;
	while(MixedTypeRelationNext(resultRelations)) {
		SessionPrintMargin();
		PrintFormActorsAsFormula(queryView.form, MixedTypeRelationPeekTuple(resultRelations), &ordering);
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

	case ASSERT_CONTRADICTION:
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

	case ASSERT_NOT_WRITABLE:
		printLine("The relation is not writable.");
		break;

	case ASSERT_INVALID_IFACT:
		printLine("An ifact rule must be a single term with one generator (*) and distinct variables.");
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
	Atom formula = ParseFormula(formulaText, 0, &errorPosition);
	if(!formula.hash) {
		printParseError(linePosition + errorPosition);
		return;
	}

	printAssertResult(AssertFormula(formula));
	ReleaseFormula(formula);
}


/*
 * CLAUDE: Retract one fact, which is the text following the :retract command. Only a term
 * with no variable is a fact; see RetractFact().
 */
static void executeRetract(char const * factText, index32 linePosition)
{
	if(!*factText) {
		printLine(":retract requires a term.");
		return;
	}

	index32 errorPosition;
	Atom fact = ParseFormula(factText, 0, &errorPosition);
	if(!fact.hash) {
		printParseError(linePosition + errorPosition);
		return;
	}

	FormulaView factView = FormulaGetView(fact);
	if(!IsTermForm(factView.form))
		printLine("Only a fact can be retracted.");
	else if(TypedTupleContainsVariable(factView.actors))
		printLine("A fact may not contain a variable.");
	else {
		RetractFact(factView);
		printLine("Ok.");
	}
	ReleaseFormula(fact);
}


/*
 * CLAUDE: Report where a load stopped and why; see LoadReport.
 */
static void printLoadError(char const * filePath, LoadReport const * report)
{
	SessionPrintMargin();
	if(report->result == LOAD_CANNOT_READ) {
		PrintF("Cannot read %s\n", filePath);
		return;
	}
	if(report->result == LOAD_NO_FORM) {
		PrintF("%s has no line of role names.\n", filePath);
		return;
	}

	PrintF("%s line %u: ", filePath, report->lineNumber);
	switch(report->result) {
	case LOAD_LINE_TOO_LONG:
		PrintF("a line may hold at most %u characters.\n", LOAD_MAX_LINE_LENGTH);
		break;

	case LOAD_SYNTAX_ERROR:
		PrintF("syntax error at column %u.\n", report->errorIndex + 1);
		break;

	case LOAD_WRONG_ARITY:
		PrintF("%u actors, but the form has %u roles.\n",
			report->nActors, TermFormArity(report->form));
		break;

	case LOAD_ASSERT_FAILED:
		PrintChar('\n');
		printAssertResult(report->assertResult);
		break;

	default:
		ASSERT(false)
		break;
	}
}


/*
 * CLAUDE: Assert the facts of the text file named by the text following the :load
 * command; see LoadRelationFile().
 */
static void executeLoad(char const * pathText)
{
	size32 pathLength = CStringLength(pathText);
	while((pathLength > 0) && IsSpaceChar(pathText[pathLength - 1]))
		pathLength--;
	if(pathLength == 0) {
		printLine(":load requires a file name.");
		return;
	}
	char filePath[pathLength + 1];
	CopyMemory(pathText, filePath, pathLength);
	filePath[pathLength] = 0;

	LoadReport report;
	LoadRelationFile(filePath, &report);
	if(report.result != LOAD_OK)
		printLoadError(filePath, &report);

	if(report.form.hash) {
		SessionPrintMargin();
		PrintF("Asserted %u facts of the form ", report.nAsserted);
		PrintTermForm(report.form);
		PrintCString(".\n");
		if(report.nExisting > 0) {
			SessionPrintMargin();
			PrintF("%u facts were already known.\n", report.nExisting);
		}
		IFactRelease(report.form);
	}
}


/*
 * Print the services the query dispatches to, which is the text following the :inspect
 * command. The query is not executed, and no service is compiled for it, so a query that
 * does not dispatch to an existing service gives no output. See dispatch.h.
 */
static void executeInspect(char const * queryText, index32 linePosition)
{
	if(!*queryText) {
		printLine(":inspect requires a formula.");
		return;
	}

	index32 errorPosition;
	Atom query = ParseFormula(queryText, 0, &errorPosition);
	if(!query.hash) {
		printParseError(linePosition + errorPosition);
		return;
	}
	FormulaView queryView = FormulaGetView(query);
	if(!IsRelationForm(queryView.form)) {
		printLine("A query must be a term or a conjunction of terms.");
		ReleaseFormula(query);
		return;
	}

	size8 arity = queryView.actors->nAtoms;
	ParameterizedQuery parameterizedQuery = {
		.form = queryView.form,
		.arity = arity
	};
	ActorsToParameters(queryView.actors, parameterizedQuery.parameters);
	index8 permutation[arity];
	
	DispatchIterator iterator;
	DispatchIterate(&parameterizedQuery, DISPATCH_MATCH_EXACT, permutation, &iterator);

	size32 nServices = 0;
	TypedTuple * permutedActors = CreateTypedTuple(arity);
	while(DispatchIteratorNext(&iterator)) {
		SessionPrintMargin();
		TypedTupleCopySubset(queryView.actors, permutation, arity, permutedActors);
		PrintServiceWithActors(
			DispatchIteratorPeekServiceRecord(&iterator)->service, permutedActors);
		PrintChar('\n');
		nServices++;
	}
	DispatchIteratorEnd(&iterator);

	printInspectSummary(nServices);
	FreeTypedTuple(permutedActors);
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

	char const * queryText = matchCommand(commandText, ":inspect");
	if(queryText) {
		executeInspect(queryText, queryText - line);
		return SESSION_CONTINUE;
	}

	char const * pathText = matchCommand(commandText, ":load");
	if(pathText) {
		executeLoad(pathText);
		return SESSION_CONTINUE;
	}

	char const * retractText = matchCommand(commandText, ":retract");
	if(retractText) {
		executeRetract(retractText, retractText - line);
		return SESSION_CONTINUE;
	}

	printLine("Unknown command. Enter :help for the commands.");
	return SESSION_CONTINUE;
}


int SessionExecuteLine(char const * line)
{
	sessionState = GetPersistentState(STATE_KEY_SESSION);
	if(!sessionState)
		initializeSession();
	IFactSetPrinter(printNumberedID);
	// Expand user @number syntax to full hashes
	if(!expandIDNumbers(line))
		return SESSION_CONTINUE;
	// The rest of the session reads the expanded line
	line = sessionState->expandedLine.buffer;

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
