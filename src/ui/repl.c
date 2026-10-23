/**
 * A read-eval-print loop for the atom system, run from the command line.
 *
 * This is the command line front end to a session: it prints the prompt, reads a line
 * from the input stream and hands the line to SessionExecuteLine(), until the session
 * ends or the input runs out. What a line means is the session's business; see
 * ui/session.h.
 */

#include "kernel/kernel.h"
#include "library/MachineService.h"
#include "library/library.h"
#include "library/string.h"
#include "parser/StringBuffer.h"
#include "platform.h"
#include "ui/session.h"


#define LINE_BUFFER_SIZE	1024


/*
 * Read command line options:
 *
 *   --transient runs with TRANSIENT_MEMORY
 *   --new runs with NEW_PERSISTENT_MEMORY
 *   --restart runs with RESTART_PERSISTENT_MEMORY
 */
static uint32 getMemoryPersistence(int argc, char * argv[])
{
	uint32 memoryPersistence = RESTART_PERSISTENT_MEMORY;
	for(int i = 1; i < argc; i++) {
		if(CStringCompare(argv[i], "--transient") == 0)
			memoryPersistence = TRANSIENT_MEMORY;
		else if(CStringCompare(argv[i], "--new") == 0)
			memoryPersistence = NEW_PERSISTENT_MEMORY;
		else if(CStringCompare(argv[i], "--restart") == 0)
			memoryPersistence = RESTART_PERSISTENT_MEMORY;
	}
	return memoryPersistence;
}


/*
 * Append a line to an entry, following a '\n' unless the entry is empty. The entry
 * is kept terminated by 0.
 */
static void appendLine(StringBuffer * entry, char const * line)
{
	if(entry->stringLength > 0) {
		// replace the terminating 0 by the line break
		entry->stringLength--;
		StringBufferPush(entry, '\n');
	}
	for(index32 i = 0; line[i]; i++)
		StringBufferPush(entry, line[i]);
	StringBufferPush(entry, 0);
}


int main(int argc, char * argv[])
{
	// A restored world holds library functions, which must be known before the restart
	RegisterLibraryFunctions();
	if(!KernelInitialize(getMemoryPersistence(argc, argv))) {
		// only a restart can fail, and the reason is printed already
		PrintF("Use --new to start a new session.\n");
		return 1;
	}
	LoadLibraries();
	SessionPrintBanner();

	char line[LINE_BUFFER_SIZE];
	// the lines read so far of an entry that is not complete; see SESSION_INCOMPLETE
	StringBuffer entry;
	StringBufferInit(&entry);
	bool isRunning = true;
	while(isRunning) {
		char const * prompt = (entry.stringLength > 0) ? SESSION_CONTINUATION_PROMPT : SESSION_PROMPT;
		switch(ReadLine(prompt, line, LINE_BUFFER_SIZE)) {
		case READLINE_OK: {
			appendLine(&entry, line);
			int result = SessionExecuteLine(entry.buffer);
			if(result != SESSION_INCOMPLETE)
				StringBufferReset(&entry);
			isRunning = (result != SESSION_QUIT);
			break;
		}

		case READLINE_TOO_LONG:
			SessionPrintStatusPrefix();
			PrintF("A line may hold at most %u characters.\n", LINE_BUFFER_SIZE - 1);
			StringBufferReset(&entry);
			break;

		case READLINE_CANCELLED:
			StringBufferReset(&entry);
			break;

		case READLINE_END:
			// end of input, from a redirected file or Ctrl-D at the terminal.
			// The prompt was printed and nothing was typed after it.
			PrintChar('\n');
			// a blank line ends an entry that is not complete, reporting the error
			if(entry.stringLength > 0) {
				appendLine(&entry, "");
				SessionExecuteLine(entry.buffer);
			}
			isRunning = false;
			break;
		}
	}
	StringBufferFree(&entry);

	// Close, and write the current world to the page file if persistence is enabled.
	KernelClose();
	return 0;
}
