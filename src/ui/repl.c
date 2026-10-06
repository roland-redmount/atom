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
#include "platform.h"
#include "ui/session.h"


#define LINE_BUFFER_SIZE	1024


/*
 * Read the command line for --transient, which keeps the session's memory to
 * itself rather than mirroring it to a paging file. That is what a WebAssembly
 * build needs, having no disk to mirror to, and it also leaves a command line
 * session with nothing to clean up afterwards.
 */
/*
 * CLAUDE: --new starts a blank world in a new paging file, replacing the paging file of
 * an earlier session. --restart continues the session from the paging file that the
 * previous session closed, and is what the repl does when given no option.
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
	bool isRunning = true;
	while(isRunning) {
		switch(ReadLine(SESSION_PROMPT, line, LINE_BUFFER_SIZE)) {
		case READLINE_OK:
			isRunning = (SessionExecuteLine(line) == SESSION_CONTINUE);
			break;

		case READLINE_TOO_LONG:
			SessionPrintMargin();
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

	// Close, and write the current world to the page file if persistence is enabled.
	KernelClose();
	return 0;
}
