/*
 * Replay a script through the WebAssembly session, printing exactly what the
 * command line repl prints for the same input, so that the two front ends can be
 * compared. Not part of the page: this exists to check that they answer alike.
 *
 *   node web/replay.mjs <module.mjs> < script.txt
 */
import { readFileSync } from "node:fs";

const SESSION_PROMPT = "> ";
const SESSION_QUIT = 2;

const modulePath = process.argv[2];
const write = (text) => process.stdout.write(text);

const factory = (await import(modulePath)).default;
// every line a session prints ends with a newline, which emscripten strips
const atom = await factory({ print: (text) => write(text + "\n") });

const initialize = atom.cwrap("WebInitialize", null, []);
const executeLine = atom.cwrap("WebExecuteLine", "number", ["string"]);

initialize();
for (const line of readFileSync(0, "utf8").split("\n")) {
    write(SESSION_PROMPT);
    if (executeLine(line) === SESSION_QUIT) break;
}
