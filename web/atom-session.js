/*
 * The one session a page talks to.
 *
 * Every snippet on a page shares it, so a page reads as a session: a fact stated
 * in one snippet is there for a later snippet to ask about. Resetting starts a
 * blank world again.
 *
 * The session runs in a worker, which is what makes a query interruptible: a
 * query that does not end can only be stopped by terminating the worker. It is
 * started when a page first runs something, so a post nobody runs costs nothing.
 */

const WORKER_PATH = "./atom-worker.js";

let worker = null;
let nextRunId = 1;
// what to do with the output and the outcome of each run still in progress
const pendingRuns = new Map();
// told whenever the session is replaced, so that stale output can be cleared
const resetListeners = new Set();

function receive(event)
{
    const { type, id, text, ended, message } = event.data;
    if(type === "ready")
        return;

    const run = pendingRuns.get(id);
    if(type === "output") {
        // output printed while starting the session carries no id
        if(run)
            run.printLine(text);
        return;
    }
    if(!run)
        return;

    pendingRuns.delete(id);
    if(type === "done")
        run.finish({ ended });
    else if(type === "failed")
        run.finish({ ended: true, message });
}

function startWorker()
{
    worker = new Worker(new URL(WORKER_PATH, import.meta.url), { type: "module" });
    worker.onmessage = receive;
    worker.onerror = (event) => {
        for(const [id, run] of pendingRuns) {
            pendingRuns.delete(id);
            run.finish({ ended: true, message: event.message ?? "the session stopped" });
        }
    };
    return worker;
}

/**
 * Discard the session and the world it holds. A run in progress is stopped,
 * which is the point: terminating the worker is the only way to end a query
 * that does not end by itself.
 */
export function resetSession()
{
    if(worker) {
        worker.terminate();
        worker = null;
    }
    for(const [id, run] of pendingRuns) {
        pendingRuns.delete(id);
        run.finish({ ended: true, message: "stopped" });
    }
    for(const listener of resetListeners)
        listener();
}

export function onSessionReset(listener)
{
    resetListeners.add(listener);
    return () => resetListeners.delete(listener);
}

/**
 * Answer the lines of a snippet, calling printLine with each line printed as it
 * is printed. Resolves once the snippet has been answered, with a message if the
 * session stopped rather than finished.
 */
export function runSnippet(source, printLine)
{
    const target = worker ?? startWorker();
    const id = nextRunId++;
    return new Promise((finish) => {
        pendingRuns.set(id, { printLine, finish });
        target.postMessage({ type: "run", id, source });
    });
}
