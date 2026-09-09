/**
 * The WebAssembly front end to a session, which a web page drives.
 *
 * There is no read loop here: a page hands over one line at a time by calling
 * WebExecuteLine(), so nothing blocks waiting for input. What a line means is the
 * session's business; see ui/session.h.
 *
 * A session prints to the output stream, which emscripten hands to the page. Every
 * line a session prints ends with a newline, so the page sees each one as it is
 * printed rather than only when the line being answered is finished.
 *
 * The world lives as long as the page holds this module. Its memory is transient,
 * there being no disk to mirror it to, so a page starts a blank world by loading
 * the module again.
 */

#include <emscripten.h>

#include "kernel/kernel.h"
#include "library/library.h"
#include "ui/session.h"


/**
 * Start a blank world and print the text a session opens with.
 * A page calls this once, before any call to WebExecuteLine().
 */
EMSCRIPTEN_KEEPALIVE
void WebInitialize(void)
{
	KernelInitialize(TRANSIENT_MEMORY);
	LoadLibraries();
	SessionPrintBanner();
}


/**
 * Answer one line, printing whatever the line asks for.
 * Returns SESSION_QUIT once the session has been asked to end, and SESSION_CONTINUE
 * while it has not. A page has nothing to answer with after SESSION_QUIT.
 */
EMSCRIPTEN_KEEPALIVE
int WebExecuteLine(char const * line)
{
	return SessionExecuteLine(line);
}
