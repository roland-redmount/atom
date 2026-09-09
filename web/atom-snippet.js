/*
 * A runnable atom snippet, for a page that wants one.
 *
 *   <atom-snippet>+ 2 + 3 = s</atom-snippet>
 *
 * The text the element starts with is the snippet, which a reader can edit and
 * run. What a run prints appears underneath. Every snippet on a page shares one
 * session, so a snippet can ask about a fact an earlier snippet stated; see
 * atom-session.js.
 */

import { runSnippet, resetSession, onSessionReset } from "./atom-session.js";

const STYLE = `
:host {
    --atom-edge: #d9d5cd;
    --atom-ground: #fbfaf8;
    --atom-ink: #24211c;
    --atom-quiet: #6d675d;
    --atom-output: #f4f2ee;
    display: block;
    margin: 1.5em 0;
    color: var(--atom-ink);
    font-size: 14px;
}
@media (prefers-color-scheme: dark) {
    :host {
        --atom-edge: #3a3833;
        --atom-ground: #1c1b18;
        --atom-ink: #e6e2da;
        --atom-quiet: #9a948a;
        --atom-output: #232220;
    }
}
.frame {
    border: 1px solid var(--atom-edge);
    border-radius: 6px;
    overflow: hidden;
    background: var(--atom-ground);
}
textarea {
    display: block;
    width: 100%;
    box-sizing: border-box;
    border: 0;
    padding: 12px 14px;
    resize: none;
    background: transparent;
    color: inherit;
    font: inherit;
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    line-height: 1.5;
}
textarea:focus { outline: 2px solid #7a9cc6; outline-offset: -2px; }
.entry {
    display: flex;
    align-items: flex-start;
}
.entry textarea { flex: 1; min-width: 0; }
.controls {
    display: flex;
    gap: 6px;
    padding: 10px 10px 10px 0;
}
.controls button { white-space: nowrap; }
button {
    font: inherit;
    padding: 4px 12px;
    border: 1px solid var(--atom-edge);
    border-radius: 4px;
    background: var(--atom-ground);
    color: inherit;
    cursor: pointer;
}
button:hover:not(:disabled) { border-color: var(--atom-quiet); }
button:disabled { opacity: 0.5; cursor: default; }
.quiet { color: var(--atom-quiet); font-size: 13px; }
.status {
    padding: 8px 14px;
    border-top: 1px solid var(--atom-edge);
}
.status:empty { display: none; }
pre {
    margin: 0;
    padding: 12px 14px;
    border-top: 1px solid var(--atom-edge);
    background: var(--atom-output);
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    line-height: 1.5;
    white-space: pre;
    overflow-x: auto;
}
pre:empty { display: none; }
`;

class AtomSnippet extends HTMLElement
{
    connectedCallback()
    {
        if(this.shadowRoot)
            return;

        const source = this.textContent.trim();
        this.textContent = "";

        const root = this.attachShadow({ mode: "open" });
        root.innerHTML = `
            <style>${STYLE}</style>
            <div class="frame">
                <div class="entry">
                    <textarea spellcheck="false" aria-label="atom snippet"></textarea>
                    <div class="controls">
                        <button class="run">Run</button>
                        <button class="reset quiet">Reset</button>
                    </div>
                </div>
                <pre aria-live="polite"></pre>
                <div class="quiet status" aria-live="polite"></div>
            </div>`;

        this.editor = root.querySelector("textarea");
        this.runButton = root.querySelector(".run");
        this.resetButton = root.querySelector(".reset");
        this.status = root.querySelector(".status");
        this.output = root.querySelector("pre");
        this.running = false;

        this.editor.value = source;
        this.editor.addEventListener("input", () => this.fitEditor());
        // a reader who has just edited a snippet most likely wants to run it
        this.editor.addEventListener("keydown", (event) => {
            if((event.key === "Enter") && (event.ctrlKey || event.metaKey)) {
                event.preventDefault();
                this.run();
            }
        });
        this.runButton.addEventListener("click", () => this.running ? resetSession() : this.run());
        this.resetButton.addEventListener("click", () => resetSession());
        // the world is not the one this output came from any more, though a
        // snippet that was interrupted keeps what it had printed by then
        onSessionReset(() => { if(!this.running) this.clear(); });

        this.fitEditor();
    }

    fitEditor()
    {
        this.editor.rows = this.editor.value.split("\n").length;
    }

    clear()
    {
        this.output.textContent = "";
        this.status.textContent = "";
    }

    async run()
    {
        if(this.running)
            return;

        this.clear();
        this.running = true;
        this.runButton.textContent = "Stop";
        this.resetButton.disabled = true;

        const lines = [];
        const outcome = await runSnippet(this.editor.value, (text) => {
            lines.push(text);
            this.output.textContent = lines.join("\n");
        });

        this.running = false;
        this.runButton.textContent = "Run";
        this.resetButton.disabled = false;
        if(outcome.message)
            this.status.textContent = `session ended: ${outcome.message}`;
        else if(outcome.ended)
            this.status.textContent = "session ended";
    }
}

customElements.define("atom-snippet", AtomSnippet);
