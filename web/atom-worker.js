/*
 * The worker an atom session runs in.
 *
 * A session holds the page's thread for as long as a query takes, and a query can
 * run forever, so it is answered here rather than on the page's own thread. The
 * page stops a query that does not end by terminating the worker, which is the
 * only way to interrupt one; see atom-snippet.js.
 *
 * Messages in:  { type: "run", id, source }
 * Messages out: { type: "ready" }
 *               { type: "output", id, text }   one line, as it is printed
 *               { type: "done", id, ended }    ended: the session was asked to quit
 *               { type: "failed", id, message }
 *
 * The id is the caller's, and is returned as given. Output printed while starting
 * the session carries no id.
 */

const SESSION_QUIT = 2;

/*
 * A session that stops the module leaves it unusable, but not obviously so: a
 * call made after that still returns, and returns a wrong answer rather than
 * failing. So a session that has failed answers nothing more, and the page is
 * left to discard this worker and start another.
 */
let failure = null;

// where the module is, overridable so that a page can serve it from elsewhere
const moduleParameter = new URL(location.href).searchParams.get("module");
const modulePath = new URL(moduleParameter ?? "./atomweb.mjs", location.href);

let currentId = null;
let executeLine = null;

// every line a session prints ends with a newline, which emscripten strips
function printLine(text)
{
    postMessage({ type: "output", id: currentId, text });
}

const started = (async () => {
    const factory = (await import(modulePath)).default;
    const atom = await factory({ print: printLine, printErr: printLine });
    executeLine = atom.cwrap("WebExecuteLine", "number", ["string"]);
    atom.ccall("WebInitialize", null, [], []);
})();

started.then(
    () => postMessage({ type: "ready" }),
    (error) => postMessage({ type: "failed", id: null, message: String(error) })
);

onmessage = async (event) => {
    const { type, id, source } = event.data;
    if(type !== "run")
        return;

    if(failure) {
        postMessage({ type: "failed", id, message: failure });
        return;
    }

    try {
        await started;
        currentId = id;
        let ended = false;
        // a snippet is answered a line at a time, as if the lines were typed
        for(const line of source.split("\n")) {
            if(executeLine(line) === SESSION_QUIT) {
                ended = true;
                break;
            }
        }
        postMessage({ type: "done", id, ended });
    }
    catch(error) {
        // the session has stopped the module, so nothing more can be answered
        failure = String(error);
        postMessage({ type: "failed", id, message: failure });
    }
    finally {
        currentId = null;
    }
};
