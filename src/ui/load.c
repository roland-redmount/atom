
#include "kernel/ifact.h"
#include "kernel/multiset.h"
#include "kernel/TypedRelation.h"
#include "kernel/typedtuple.h"
#include "lang/name.h"
#include "lang/PredicateForm.h"
#include "lang/TermForm.h"
#include "parser/Characters.h"
#include "parser/TupleParser.h"
#include "ui/assert.h"
#include "ui/load.h"


#define READ_CHUNK_SIZE		4096

// CLAUDE: A line of n characters has at most n + 1 columns; see SplitColumns()
#define MAX_COLUMNS			(LOAD_MAX_LINE_LENGTH + 1)

// CLAUDE: the byte order mark that a UTF-8 file may begin with
static char const byteOrderMark[] = "\xEF\xBB\xBF";


/*
 * CLAUDE: A Loader collects characters into lines and loads one line at a time.
 * Loading continues while report->result is LOAD_OK.
 */
typedef struct s_Loader {
	LoadReport * report;
	Atom form;
	size8 arity;
	bool isHeaderRead;
	char separator;
	// CLAUDE: the column read by each actor of the form, in the order of the form
	index32 columnOfActor[RELATION_MAX_ARITY];
	Column columns[MAX_COLUMNS];
	char line[LOAD_MAX_LINE_LENGTH + 1];
	size32 lineLength;
} Loader;


static void initializeLoader(Loader * loader, Atom form, LoadReport * report)
{
	*report = (LoadReport) {.result = LOAD_OK};
	loader->report = report;
	loader->form = form;
	loader->arity = TermFormArity(form);
	loader->isHeaderRead = false;
	loader->lineLength = 0;
}


static bool isBlank(char const * line)
{
	while(*line == ' ')
		line++;
	return !*line;
}


/*
 * CLAUDE: The separator of the columns of the first line is the first character that is
 * neither a name character nor a space. A line of names and spaces only is separated by spaces.
 */
static char findSeparator(char const * header)
{
	for(index32 i = 0; header[i]; i++) {
		if(!IsNameChar(header[i]) && (header[i] != ' '))
			return header[i];
	}
	return ' ';
}


/*
 * CLAUDE: Read the role name of each column of the first line, and set the column read by
 * each actor of the form. The roles of one name are adjacent in the form, beginning at the
 * position given by PredicateFindRolePosition(). The n-th column of a name is read by the n-th
 * role of that name. A column whose name is not a role of the form is not read.
 */
static void loadHeaderLine(Loader * loader)
{
	LoadReport * report = loader->report;
	char const * header = loader->line;
	bool hasByteOrderMark = (header[0] == byteOrderMark[0]) &&
		(header[1] == byteOrderMark[1]) && (header[2] == byteOrderMark[2]);
	if(hasByteOrderMark)
		header += 3;
	if(isBlank(header)) {
		report->result = LOAD_NO_HEADER;
		return;
	}

	loader->separator = findSeparator(header);
	size32 nColumns = SplitColumns(header, loader->separator, loader->columns, MAX_COLUMNS);
	report->nHeaderColumns = nColumns;

	// CLAUDE: the number of columns found for the roles of each name, at the first role of the name
	size8 nFound[RELATION_MAX_ARITY];
	SetMemory(nFound, sizeof(nFound), 0);
	Atom predicateForm = TermFormGetPredicateForm(loader->form);
	for(index32 k = 0; k < nColumns; k++) {
		Column column = loader->columns[k];
		char const * columnName = header + column.start;
		bool isName = (column.length > 0) && IsNameInitialChar(columnName[0]) &&
			IsNameString(columnName, column.length);
		if(!isName) {
			report->result = LOAD_SYNTAX_ERROR;
			report->errorIndex = column.start;
			return;
		}
		Atom name = CreateName(columnName, column.length);
		uint8 rolePosition = PredicateFindRolePosition(predicateForm, name);
		if(rolePosition) {
			index8 firstRole = rolePosition - 1;
			if(nFound[firstRole] < MultisetGetElementMultiple(predicateForm, name)) {
				loader->columnOfActor[firstRole + nFound[firstRole]] = k;
				nFound[firstRole]++;
			}
		}
		NameRelease(name);
	}

	// CLAUDE: report the first role name with too few columns
	MultisetIterator iterator;
	MultisetIterate(predicateForm, AT_NAME, &iterator);
	index8 firstRole = 0;
	while(MultisetIteratorNext(&iterator)) {
		ElementMultiple role = MultisetIteratorGetElement(&iterator);
		if(nFound[firstRole] < role.multiple) {
			report->result = LOAD_MISSING_COLUMN;
			report->missingRole = role.element;
			break;
		}
		firstRole += role.multiple;
	}
	MultisetIteratorEnd(&iterator);
	loader->isHeaderRead = true;
}


static void assertActors(Loader * loader, TypedAtom const actors[])
{
	LoadReport * report = loader->report;
	TypedTuple * tuple = CreateTypedTupleFromArray(actors, loader->arity);
	int assertResult = AssertFact(CreateFormulaView(loader->form, tuple), 0);
	FreeTypedTuple(tuple);

	if(assertResult == ASSERT_OK)
		report->nAsserted++;
	else if(assertResult == ASSERT_EXISTED)
		report->nExisting++;
	else {
		report->result = LOAD_ASSERT_FAILED;
		report->assertResult = assertResult;
	}
}


/*
 * CLAUDE: Read the actor of each role from the column of the role, and assert the fact.
 * A line with an empty column read by a role is skipped.
 */
static void loadFactLine(Loader * loader)
{
	LoadReport * report = loader->report;
	report->nColumns = SplitColumns(loader->line, loader->separator, loader->columns, MAX_COLUMNS);
	if(report->nColumns != report->nHeaderColumns) {
		report->result = LOAD_WRONG_COLUMN_COUNT;
		return;
	}

	TypedAtom actors[RELATION_MAX_ARITY];
	size8 nActors = 0;
	bool isEmpty = false;
	while((nActors < loader->arity) && !isEmpty) {
		Column column = loader->columns[loader->columnOfActor[nActors]];
		if(!ParseColumnActor(loader->line, column, &actors[nActors], &isEmpty, &report->errorIndex)) {
			report->result = LOAD_SYNTAX_ERROR;
			break;
		}
		if(!isEmpty)
			nActors++;
	}
	if(report->result == LOAD_OK) {
		if(isEmpty)
			report->nSkipped++;
		else
			assertActors(loader, actors);
	}
	for(index8 i = 0; i < nActors; i++)
		ReleaseTypedAtom(actors[i]);
}


static void loadLine(Loader * loader)
{
	loader->line[loader->lineLength] = 0;
	loader->lineLength = 0;
	loader->report->lineNumber++;
	if(!loader->isHeaderRead)
		loadHeaderLine(loader);
	else if(!isBlank(loader->line))
		loadFactLine(loader);
}


static void pushCharacter(Loader * loader, char c)
{
	if(c == '\n') {
		loadLine(loader);
		return;
	}
	// CLAUDE: a line ending in "\r\n" is read as ending in "\n"
	if(c == '\r')
		return;
	if(loader->lineLength == LOAD_MAX_LINE_LENGTH) {
		loader->report->lineNumber++;
		loader->report->result = LOAD_LINE_TOO_LONG;
		return;
	}
	loader->line[loader->lineLength++] = c;
}


/*
 * CLAUDE: Load the last line, which need not end in a newline.
 */
static void finishLoader(Loader * loader)
{
	if(loader->lineLength > 0)
		loadLine(loader);
	if((loader->report->result == LOAD_OK) && !loader->isHeaderRead)
		loader->report->result = LOAD_NO_HEADER;
}


void LoadRelationText(char const * text, Atom form, LoadReport * report)
{
	Loader loader;
	initializeLoader(&loader, form, report);
	for(index32 i = 0; text[i] && (report->result == LOAD_OK); i++)
		pushCharacter(&loader, text[i]);
	if(report->result == LOAD_OK)
		finishLoader(&loader);
}


void LoadRelationFile(char const * filePath, Atom form, LoadReport * report)
{
	Loader loader;
	initializeLoader(&loader, form, report);
	FileHandle file = OpenFile(filePath);
	if(!file) {
		report->result = LOAD_CANNOT_READ;
		return;
	}

	char chunk[READ_CHUNK_SIZE];
	size64 nBytesLeft = GetFileSize(file);
	while((nBytesLeft > 0) && (report->result == LOAD_OK)) {
		size32 chunkSize = (nBytesLeft < READ_CHUNK_SIZE) ? nBytesLeft : READ_CHUNK_SIZE;
		if(!ReadFromFile(file, chunk, chunkSize)) {
			report->result = LOAD_CANNOT_READ;
			break;
		}
		for(index32 i = 0; (i < chunkSize) && (report->result == LOAD_OK); i++)
			pushCharacter(&loader, chunk[i]);
		nBytesLeft -= chunkSize;
	}
	CloseFile(file);
	if(report->result == LOAD_OK)
		finishLoader(&loader);
}
