/**
 * A session answering the lines a front end reads. See ui/session.h.
 */

#include "kernel/dictionary.h"
#include "kernel/dispatch.h"
#include "kernel/ifact.h"
#include "kernel/Parameter.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/Parameter.h"
#include "lang/formula.h"
#include "lang/name.h"
#include "ui/assert.h"
#include "ui/load.h"
#include "lang/TermForm.h"
#include "lang/Variable.h"
#include "library/string.h"
#include "parser/Characters.h"
#include "parser/FormulaBuilder.h"
#include "parser/StringBuffer.h"
#include "parser/TermFormBuilder.h"
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
	// CLAUDE: the length of the replacement in the expanded line; see expandIDNames()
	size32 expandedLength;
} IDExpansion;

/*
 * A NamedID maps a name entered by the user to an ID atom. The user enters the name
 * as @name in queries and commands, and the session prints the ID atom as @name.
 */
typedef struct s_NamedID {
	char * name;		// without the @ character
	Atom id;
} NamedID;

/*
 * A NewName is a @name in a query line that names no ID atom yet. The query line is
 * expanded with the anonymous variable _ in place of the @name. The anonymous variable is
 * the anonymousIndex-th anonymous variable of the query, counted in the order entered.
 * executeQuery() binds the name to the ID atom found in place of that anonymous variable.
 */
typedef struct s_NewName {
	char const * name;	// points to the char after the '@' in the line entered
	size32 nameLength;
	index32 anonymousIndex;
} NewName;

typedef struct s_SessionState {
	/*
	 * Each NamedID of the session. The session holds a reference to each named ID atom,
	 * so that the name stays valid until the user removes it with :clear.
	 * NOTE: the same ID atom may have more than one name.
	 */
	ResizingArray namedIDs;

	// The line input expanded so that each ID atom number is replaced by the hash of the ID atom.
	StringBuffer expandedLine;
	// Each IDExpansion in idExpansions records one replacement, in the order of the line.
	ResizingArray idExpansions;
	// CLAUDE: Each NewName of the line, in the order of the line
	ResizingArray newNames;
} SessionState;

/* Session state, persisted in STATE_KEY_SESSION. SessionExecuteLine() sets
   sessionState, and creates the module state on the first line of a new world. */
static SessionState * sessionState = 0;


static void initializeSession(void)
{
	sessionState = Allocate(sizeof(SessionState));
	SetPersistentState(STATE_KEY_SESSION, sessionState);
	CreateResizingArray(&sessionState->namedIDs, sizeof(NamedID), 16);
	CreateResizingArray(&sessionState->idExpansions, sizeof(IDExpansion), 4);
	CreateResizingArray(&sessionState->newNames, sizeof(NewName), 4);
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
	printLine("atom v" ATOM_VERSION);
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
	printLine("An ID atom is printed as @name once named. A query naming an unknown @name in place");
	printLine("of an actor names the ID atom found there, if the query has a single result.");
	PrintChar('\n');
	printLine("Commands:");
	printLine("  :assert <term>        Assert a fact. The term must not contain variables.");
	printLine("  :assert <clause>      Assert a rule. The clause must have at least two terms");
	printLine("                        and contain at least one variable.");
	printLine("  :assert <term with *> Assert an ifact rule, such as (circle * radius r).");
	printLine("  :clear @name          Remove the name @name, so that it can name another ID atom.");
	printLine("  :inspect <formula>    Print the service(s) that the formula dispatches to");
	printLine("  :load <file> <role names> Assert facts of the term form with the given role names.");
	printLine("                        The first line of the file names the columns, separated");
	printLine("                        by commas or spaces. Each role reads the column of its name.");
	printLine("  :retract <term>       Retract a fact. The term must not contain variables.");
	printLine("  :retract <clause>     Retract a rule. The variable names must match the asserted rule.");
	printLine("  :retract <term with *> Retract an ifact rule.");
	printLine("  :rules <role names>   Print the rules containing a term form, such as (foo bar)");
	printLine("                        or (! foo bar). Ifact rules are not printed.");
	printLine("  :help                 Print this text.");
	printLine("  :quit, ctrl-D         End the session.");
	PrintChar('\n');
}


/*
 * Return the NamedID with the given name, or 0 if there is none.
 */
static NamedID * findNamedIDByName(char const * name, size32 length)
{
	for(index32 i = 0; i < sessionState->namedIDs.nElements; i++) {
		NamedID * namedID = ResizingArrayGetElement(&sessionState->namedIDs, i);
		if((CStringLength(namedID->name) == length) && (CompareMemory(namedID->name, name, length) == 0))
			return namedID;
	}
	return 0;
}


/*
 * Return the first NamedID of an ID atom, or 0 if the ID atom has no name.
 */
static NamedID * findNamedIDByID(Atom id)
{
	for(index32 i = 0; i < sessionState->namedIDs.nElements; i++) {
		NamedID * namedID = ResizingArrayGetElement(&sessionState->namedIDs, i);
		if(SameAtoms(namedID->id, id))
			return namedID;
	}
	return 0;
}


/*
 * Bind a name to an ID atom. The name must not be bound to an ID atom already.
 */
static void addNamedID(char const * name, size32 length, Atom id)
{
	ASSERT(!findNamedIDByName(name, length))
	NamedID namedID = {
		.name = Allocate(length + 1),
		.id = id,
	};
	CopyMemory(name, namedID.name, length);
	namedID.name[length] = 0;
	IFactAcquire(id);
	ResizingArrayAppend(&sessionState->namedIDs, &namedID);
}


/*
 * Print an ID atom as @name. Returns false, having printed nothing, if the
 * ID atom has no name; see IFactSetPrinter().
 */
static bool printNamedID(Atom id)
{
	NamedID const * namedID = findNamedIDByID(id);
	if(!namedID)
		return false;
	PrintChar('@');
	PrintCString(namedID->name);
	return true;
}


/*
 * Print a name entered by the user, with the @ character.
 */
static void printEnteredName(char const * name, size32 length)
{
	PrintChar('@');
	PrintCharString(name, length);
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
		if(expandedPosition < expandedStart + expansion->expandedLength)
			return expansion->linePosition;
		growth += expansion->expandedLength - expansion->inputLength;
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
 * CLAUDE: Point at the character of the line entered where a @name went wrong, like
 * printParseError() does, and print the message after the caret.
 */
static void printNameError(index32 linePosition, char const * message)
{
	SessionPrintMargin();
	for(index32 i = 0; i < linePosition; i++)
		PrintChar(' ');
	PrintCString("^ ");
	PrintCString(message);
	PrintChar('\n');
}


/*
 * CLAUDE: Return the NewName of the line with the given name, or 0 if there is none.
 */
static NewName const * findNewName(char const * name, size32 length)
{
	for(index32 i = 0; i < sessionState->newNames.nElements; i++) {
		NewName const * newName = ResizingArrayGetElement(&sessionState->newNames, i);
		if((newName->nameLength == length) && (CompareMemory(newName->name, name, length) == 0))
			return newName;
	}
	return 0;
}


/*
 * CLAUDE: Copy an input line to expandedLine, replacing each @name. A @name is @ at the start
 * of a word and outside of a string, followed by a name; see IsNameInitialChar(). A @name of a
 * NamedID is replaced by the hash of the ID atom, written as @ followed by ID_HASH_LENGTH hex
 * digits; see TOKEN_ID. A hash entered by the user is copied as is.
 *
 * In a query line (isQuery), a @name of no NamedID is replaced by the anonymous variable _,
 * and recorded as a NewName. Such a @name may occur only once in the line, and not inside
 * [ ]. In any other line, a @name of no NamedID is an error.
 *
 * Other text is copied as is, and left for ParseFormula() to judge. Returns false, having
 * printed an error, if a @name cannot be replaced.
 */
static bool expandIDNames(char const * line, bool isQuery)
{
	StringBufferReset(&sessionState->expandedLine);
	ResizingArrayReset(&sessionState->idExpansions);
	ResizingArrayReset(&sessionState->newNames);
	bool isInString = false;
	size32 bracketDepth = 0;
	// CLAUDE: the number of anonymous variables outside [ ] so far; see NewName
	index32 nAnonymous = 0;
	index32 i = 0;
	while(line[i]) {
		if(line[i] == '"')
			isInString = !isInString;
		if(!isInString) {
			// CLAUDE: a letter literal 'c may be any character, such as '" or '_
			if((line[i] == '\'') && line[i + 1]) {
				StringBufferPush(&sessionState->expandedLine, line[i++]);
				StringBufferPush(&sessionState->expandedLine, line[i++]);
				continue;
			}
			if(line[i] == '[')
				bracketDepth++;
			if((line[i] == ']') && (bracketDepth > 0))
				bracketDepth--;
			if((line[i] == '_') && (bracketDepth == 0))
				nAnonymous++;
		}

		bool isWordStart = (i == 0) || !IsNameChar(line[i - 1]);
		if((line[i] != '@') || isInString || !isWordStart) {
			StringBufferPush(&sessionState->expandedLine, line[i++]);
			continue;
		}

		char const * name = line + i + 1;
		size32 length = 0;
		bool isHex = true;
		while(IsNameChar(name[length])) {
			isHex = isHex && IsHexDigitChar(name[length]);
			length++;
		}
		bool isHash = isHex && (length == ID_HASH_LENGTH);
		if(isHash || !IsNameInitialChar(name[0])) {
			StringBufferPush(&sessionState->expandedLine, line[i++]);
			continue;
		}

		IDExpansion expansion = {
			.linePosition = i,
			.inputLength = 1 + length,
		};
		NamedID const * namedID = findNamedIDByName(name, length);
		if(namedID) {
			char hashString[ID_HASH_LENGTH + 2];
			FormatString(hashString, sizeof(hashString), "@%016llx", (unsigned long long) namedID->id.hash);
			for(index32 j = 0; hashString[j]; j++)
				StringBufferPush(&sessionState->expandedLine, hashString[j]);
			expansion.expandedLength = 1 + ID_HASH_LENGTH;
		}
		else {
			if(!isQuery) {
				printNameError(i, "no ID atom has this name");
				return false;
			}
			if(bracketDepth > 0) {
				printNameError(i, "a new name cannot be inside [ ]");
				return false;
			}
			if(findNewName(name, length)) {
				printNameError(i, "a new name can occur only once in a query");
				return false;
			}
			StringBufferPush(&sessionState->expandedLine, '_');
			expansion.expandedLength = 1;
			NewName newName = {
				.name = name,
				.nameLength = length,
				.anonymousIndex = nAnonymous++,
			};
			ResizingArrayAppend(&sessionState->newNames, &newName);
		}
		ResizingArrayAppend(&sessionState->idExpansions, &expansion);
		i += 1 + length;
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


/*
 * CLAUDE: Write the index in the query actors of the anonymous variable of each NewName, in
 * the order of newNames; see NewName.
 */
static void findNewNameActors(FormulaView queryView, FormOrdering const * ordering, index8 actorIndices[])
{
	size8 arity = FormArity(queryView.form);
	index8 enteredActorIndices[arity];
	FormOrderingGetEnteredActors(queryView.form, ordering, enteredActorIndices);
	index32 nAnonymous = 0;
	for(index8 k = 0; k < arity; k++) {
		TypedAtom actor = TypedTupleGetElement(queryView.actors, enteredActorIndices[k]);
		if((actor.type != AT_VARIABLE) || (actor.atom.hash != anonymousVariable.atom.hash))
			continue;
		for(index32 i = 0; i < sessionState->newNames.nElements; i++) {
			NewName const * newName = ResizingArrayGetElement(&sessionState->newNames, i);
			if(newName->anonymousIndex == nAnonymous)
				actorIndices[i] = enteredActorIndices[k];
		}
		nAnonymous++;
	}
}


/*
 * CLAUDE: Bind each NewName to the ID atom at its actor index in the single result of a query.
 */
static void addNewNames(TypedTuple const * result, index8 const actorIndices[])
{
	for(index32 i = 0; i < sessionState->newNames.nElements; i++) {
		NewName const * newName = ResizingArrayGetElement(&sessionState->newNames, i);
		TypedAtom actor = TypedTupleGetElement(result, actorIndices[i]);
		if(actor.type == AT_ID)
			addNamedID(newName->name, newName->nameLength, actor.atom);
	}
}


/*
 * CLAUDE: Report each NewName that executeQuery() could not bind to an ID atom.
 */
static void printNewNamesNotAdded(size32 nTuples)
{
	for(index32 i = 0; i < sessionState->newNames.nElements; i++) {
		NewName const * newName = ResizingArrayGetElement(&sessionState->newNames, i);
		if(findNamedIDByName(newName->name, newName->nameLength))
			continue;
		SessionPrintMargin();
		if(nTuples == 0)
			PrintCString("No result, so ");
		else if(nTuples > 1)
			PrintF("%u results, so ", nTuples);
		else
			PrintCString("The result is not an ID atom, so ");
		printEnteredName(newName->name, newName->nameLength);
		PrintCString(" was not named.\n");
	}
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
	bool hasTuple = MixedTypeRelationNext(resultRelations);

	// CLAUDE: A NewName is bound only if the query has a single result. Reading the next result
	// overwrites the first, so the first result is copied.
	bool hasNewNames = (sessionState->newNames.nElements > 0);
	if(hasNewNames && hasTuple) {
		index8 actorIndices[sessionState->newNames.nElements];
		findNewNameActors(queryView, &ordering, actorIndices);
		TypedTuple * firstResult = CreateTupleFromTuple(MixedTypeRelationPeekTuple(resultRelations));
		hasTuple = MixedTypeRelationNext(resultRelations);
		if(!hasTuple)
			addNewNames(firstResult, actorIndices);
		SessionPrintMargin();
		PrintFormActorsAsFormula(queryView.form, firstResult, &ordering);
		PrintChar('\n');
		nTuples++;
		FreeTypedTuple(firstResult);
	}

	while(hasTuple) {
		SessionPrintMargin();
		PrintFormActorsAsFormula(queryView.form, MixedTypeRelationPeekTuple(resultRelations), &ordering);
		PrintChar('\n');
		nTuples++;
		hasTuple = MixedTypeRelationNext(resultRelations);
	}
	// the relation counts the services it read, so it is read out before being freed
	printQueryResultSummary(resultRelations, nTuples);
	if(hasNewNames)
		printNewNamesNotAdded(nTuples);
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
		printLine("Ok.");
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

	case ASSERT_NOT_CLOSABLE:
		printLine("The relation cannot be closed: the actor must be a term form whose opposite relation is neither closed nor stored.");
		break;

	case ASSERT_RESERVED_FORM:
		printLine("A rule cannot derive the relation (closed-relation f).");
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
 * Report the result of retracting a formula.
 */
static void printRetractResult(int result)
{
	switch(result) {
	case RETRACT_OK:
		printLine("Ok.");
		break;

	case RETRACT_NOT_FOUND:
		printLine("No such rule.");
		break;

	case RETRACT_TERM_VARIABLE:
		printLine("A fact may not contain a variable.");
		break;

	case RETRACT_INVALID_IFACT:
		printLine("An ifact rule must be a single term with one generator (*) and distinct variables.");
		break;

	case RETRACT_NOT_CLAUSE:
		printLine("Only a fact or a rule can be retracted.");
		break;

	default:
		ASSERT(false)
		break;
	}
}


/*
 * Retract a fact, a clause rule or an ifact rule.
 */
static void executeRetract(char const * formulaText, index32 linePosition)
{
	if(!*formulaText) {
		printLine(":retract requires a term or a clause.");
		return;
	}

	index32 errorPosition;
	Atom formula = ParseFormula(formulaText, 0, &errorPosition);
	if(!formula.hash) {
		printParseError(linePosition + errorPosition);
		return;
	}

	printRetractResult(RetractFormula(formula));
	ReleaseFormula(formula);
}


/*
 * Report where a load failed and why; see LoadReport.
 */
static void printLoadError(char const * filePath, LoadReport const * report)
{
	SessionPrintMargin();
	if(report->result == LOAD_CANNOT_READ) {
		PrintF("Cannot read %s\n", filePath);
		return;
	}
	if(report->result == LOAD_NO_HEADER) {
		PrintF("%s has no first line of column names.\n", filePath);
		return;
	}
	if(report->result == LOAD_MISSING_COLUMN) {
		PrintF("The first line of %s has too few columns named ", filePath);
		PrintName(report->missingRole);
		PrintCString(".\n");
		return;
	}

	PrintF("%s line %u: ", filePath, report->lineNumber);
	switch(report->result) {
	case LOAD_LINE_TOO_LONG:
		PrintF("a line may hold at most %u characters.\n", LOAD_MAX_LINE_LENGTH);
		break;

	case LOAD_SYNTAX_ERROR:
		PrintF("syntax error at character %u.\n", report->errorIndex + 1);
		break;

	case LOAD_WRONG_COLUMN_COUNT:
		PrintF("%u columns, but the first line has %u.\n",
			report->nColumns, report->nHeaderColumns);
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
 * Assert the facts encoded in the text file named by the text following the :load
 * command; see LoadRelationFile().
 */
static void executeLoad(char const * argumentText, index32 linePosition)
{
	// CLAUDE: the file name is the first word, and the role names of the term form follow it
	size32 pathLength = 0;
	while(argumentText[pathLength] && !IsSpaceChar(argumentText[pathLength]))
		pathLength++;
	char const * termFormText = argumentText + pathLength;
	while(IsSpaceChar(*termFormText))
		termFormText++;
	if((pathLength == 0) || !*termFormText) {
		printLine(":load requires a file name and role names.");
		return;
	}
	char filePath[pathLength + 1];
	CopyMemory(argumentText, filePath, pathLength);
	filePath[pathLength] = 0;

	index32 errorIndex;
	Atom form = ParseTermForm(termFormText, 0, &errorIndex);
	if(!form.hash) {
		printParseError(linePosition + (termFormText - argumentText) + errorIndex);
		return;
	}

	LoadReport report;
	LoadRelationFile(filePath, form, &report);
	if(report.result != LOAD_OK)
		printLoadError(filePath, &report);

	if((report.result == LOAD_OK) || (report.nAsserted + report.nExisting + report.nSkipped > 0)) {
		SessionPrintMargin();
		PrintF("Asserted %u facts of the form ", report.nAsserted);
		PrintReflectedTermForm(form);
		PrintCString(".\n");
		if(report.nExisting > 0) {
			SessionPrintMargin();
			PrintF("%u facts were already known.\n", report.nExisting);
		}
		if(report.nSkipped > 0) {
			SessionPrintMargin();
			PrintF("Skipped %u lines with missing values.\n", report.nSkipped);
		}
	}
	IFactRelease(form);
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
 * Print each clause rule in the dictionary containing the term form written by the
 * text following the :rules command, such as "foo bar" or "! foo bar". Ifact rules are not
 * printed.
 */
static void executeRules(char const * termFormText, index32 linePosition)
{
	if(!*termFormText) {
		printLine(":rules requires a term form, such as \"foo\" or \"!foo bar\".");
		return;
	}

	index32 errorIndex;
	Atom termForm = ParseTermForm(termFormText, 0, &errorIndex);
	if(!termForm.hash) {
		printParseError(linePosition + errorIndex);
		return;
	}

	ResizingArray clauseForms;
	CreateResizingArray(&clauseForms, sizeof(Atom), 4);
	DictionaryFindClauseForms(termForm, &clauseForms);

	size32 nRules = 0;
	for(index32 i = 0; i < clauseForms.nElements; i++) {
		Atom clauseForm = *(Atom const *) ResizingArrayGetElement(&clauseForms, i);
		DictionaryIterator iterator;
		DictionaryIterateClauses(clauseForm, &iterator);
		while(DictionaryIteratorNext(&iterator)) {
			SessionPrintMargin();
			PrintFormActorsAsFormula(clauseForm, DictionaryIteratorPeekActors(&iterator), 0);
			PrintChar('\n');
			nRules++;
		}
		DictionaryIteratorEnd(&iterator);
	}
	FreeResizingArray(&clauseForms);

	if(nRules == 0)
		printLine("No rules found.");
	IFactRelease(termForm);
}


/*
 * Remove the name entered after the :clear command, so that the name can be bound
 * to another ID atom. The session releases its reference to the ID atom.
 */
static void executeClear(char const * nameText, index32 linePosition)
{
	size32 length = 0;
	if(nameText[0] == '@') {
		while(IsNameChar(nameText[1 + length]))
			length++;
	}
	char const * rest = nameText + 1 + length;
	while(IsSpaceChar(*rest))
		rest++;
	if((length == 0) || *rest) {
		printLine(":clear requires a name, such as @myCar.");
		return;
	}

	NamedID * namedID = findNamedIDByName(nameText + 1, length);
	if(!namedID) {
		printNameError(linePosition, "no ID atom has this name");
		return;
	}
	IFactRelease(namedID->id);
	Free(namedID->name);
	index32 index = namedID - (NamedID *) ResizingArrayGetMemory(&sessionState->namedIDs);
	ResizingArrayRemoveElement(&sessionState->namedIDs, index);
	printLine("Ok.");
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
		executeLoad(pathText, pathText - line);
		return SESSION_CONTINUE;
	}

	char const * termFormText = matchCommand(commandText, ":rules");
	if(termFormText) {
		executeRules(termFormText, termFormText - line);
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
	IFactSetPrinter(printNamedID);

	// :clear reads its @name as entered, so it is matched before the line is expanded
	char const * enteredFirstCharacter = line;
	while(IsSpaceChar(*enteredFirstCharacter))
		enteredFirstCharacter++;
	char const * clearText = matchCommand(enteredFirstCharacter, ":clear");
	if(clearText) {
		executeClear(clearText, clearText - line);
		return SESSION_CONTINUE;
	}

	// Expand each @name; see expandIDNames()
	bool isQuery = (*enteredFirstCharacter != ':');
	if(!expandIDNames(line, isQuery))
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
