
#include "kernel/ifact.h"
#include "kernel/Relation.h"
#include "kernel/typedtuple.h"
#include "lang/TermForm.h"
#include "parser/Characters.h"
#include "parser/TermFormBuilder.h"
#include "parser/TupleParser.h"
#include "ui/assert.h"
#include "ui/load.h"
#include "util/sort.h"


#define READ_CHUNK_SIZE		4096


/*
 * CLAUDE: A Loader collects characters into lines and loads one line at a time.
 * Loading continues while report->result is LOAD_OK.
 */
typedef struct s_Loader {
	LoadReport * report;
	index8 roleOrder[RELATION_MAX_ARITY];
	size8 arity;
	char line[LOAD_MAX_LINE_LENGTH + 1];
	size32 lineLength;
} Loader;


static void initializeLoader(Loader * loader, LoadReport * report)
{
	*report = (LoadReport) {.result = LOAD_OK};
	loader->report = report;
	loader->arity = 0;
	loader->lineLength = 0;
}


static bool isBlank(char const * line)
{
	while(IsWhiteSpace(*line))
		line++;
	return !*line;
}


static void loadFormLine(Loader * loader)
{
	LoadReport * report = loader->report;
	report->form = ParseTermForm(loader->line, loader->roleOrder, &report->errorIndex);
	if(!report->form.hash) {
		report->result = LOAD_SYNTAX_ERROR;
		return;
	}
	loader->arity = TermFormArity(report->form);
}


static void loadFactLine(Loader * loader)
{
	LoadReport * report = loader->report;
	TypedAtom actors[RELATION_MAX_ARITY];
	size8 nActors;
	if(!ParseActors(loader->line, actors, RELATION_MAX_ARITY, &nActors, &report->errorIndex)) {
		report->result = LOAD_SYNTAX_ERROR;
		return;
	}
	if(nActors != loader->arity) {
		report->result = LOAD_WRONG_ARITY;
		report->nActors = nActors;
	}
	else {
		// CLAUDE: the actors are written in the order of the role names; see ParseTermForm()
		ReorderArray(actors, loader->roleOrder, nActors, sizeof(TypedAtom));
		TypedTuple * tuple = CreateTypedTupleFromArray(actors, nActors);
		int assertResult = AssertFact((FormulaView) {.form = report->form, .actors = tuple}, 0);
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
	for(index8 i = 0; i < nActors; i++)
		ReleaseTypedAtom(actors[i]);
}


static void loadLine(Loader * loader)
{
	loader->line[loader->lineLength] = 0;
	loader->lineLength = 0;
	loader->report->lineNumber++;
	if(isBlank(loader->line))
		return;
	if(!loader->report->form.hash)
		loadFormLine(loader);
	else
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
	loader->line[loader->lineLength++] = (c == '\t') ? ' ' : c;
}


/*
 * CLAUDE: Load the last line, which need not end in a newline.
 */
static void finishLoader(Loader * loader)
{
	if(loader->lineLength > 0)
		loadLine(loader);
	if((loader->report->result == LOAD_OK) && !loader->report->form.hash)
		loader->report->result = LOAD_NO_FORM;
}


void LoadRelationText(char const * text, LoadReport * report)
{
	Loader loader;
	initializeLoader(&loader, report);
	for(index32 i = 0; text[i] && (report->result == LOAD_OK); i++)
		pushCharacter(&loader, text[i]);
	if(report->result == LOAD_OK)
		finishLoader(&loader);
}


void LoadRelationFile(char const * filePath, LoadReport * report)
{
	Loader loader;
	initializeLoader(&loader, report);
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
