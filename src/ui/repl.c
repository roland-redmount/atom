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
static uint32 getMemoryPersistence(int argc, char * argv[])
{
	for(int i = 1; i < argc; i++)
		if(CStringCompare(argv[i], "--transient") == 0)
			return TRANSIENT_MEMORY;
	return PERSISTENT_MEMORY;
}


int main(int argc, char * argv[])
{
	KernelInitialize(getMemoryPersistence(argc, argv));
	LoadLibraries();
	SessionPrintBanner();

	char line[LINE_BUFFER_SIZE];
	bool isRunning = true;
	while(isRunning) {
		PrintCString(SESSION_PROMPT);
		switch(ReadLine(line, LINE_BUFFER_SIZE)) {
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

	// The world a session built is not emptied again, and KernelShutdown() is not called:
	// it requires every fact and rule above the core set to have been removed first, which
	// is a thing to do for a test rather than for a session. Nothing here outlives the
	// process; see KernelInitialize(), which starts each session with a blank world.
	return 0;
}
