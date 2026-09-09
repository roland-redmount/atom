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
/*
 * The page decides how this looks. Colours and fonts are inherited, and what
 * cannot be inherited is mixed from the inherited text colour, so that the
 * element sits in a page of any theme without being told about it.
 *
 * A page that wants the element to match its own code blocks maps its settings
 * onto these:
 *
 *   --atom-surface     what the frame is filled with
 *   --atom-button      what a button is filled with
 *   --atom-border      the lines within the frame
 *   --atom-muted       secondary text, such as the reset button
 *   --atom-code-font   the font a snippet and its output are set in
 *   --atom-code-size   the size they are set at
 *   --atom-radius      how rounded the frame is
 */
:host {
    display: block;
    margin: 1.5em 0;
    --surface: var(--atom-surface, color-mix(in srgb, currentColor 6%, transparent));
    --edge: var(--atom-border, color-mix(in srgb, currentColor 18%, transparent));
    --muted: var(--atom-muted, color-mix(in srgb, currentColor 65%, transparent));
    --button: var(--atom-button, color-mix(in srgb, currentColor 14%, transparent));
}
.frame {
    background: var(--surface);
    border-radius: var(--atom-radius, 6px);
    overflow: hidden;
}
.entry {
    display: flex;
    align-items: flex-start;
}
.entry textarea { flex: 1; min-width: 0; }
textarea, pre {
    font-family: var(--atom-code-font, monospace);
    font-size: var(--atom-code-size, 0.9em);
    line-height: 1.5;
}
textarea {
    display: block;
    width: 100%;
    box-sizing: border-box;
    border: 0;
    padding: 1rem;
    resize: none;
    background: transparent;
    color: inherit;
}
textarea:focus {
    outline: 2px solid color-mix(in srgb, currentColor 40%, transparent);
    outline-offset: -2px;
}
.controls {
    display: flex;
    gap: 6px;
    padding: 0.75rem 0.75rem 0 0;
}
button {
    font: inherit;
    font-size: 0.85em;
    color: inherit;
    background: var(--button);
    padding: 4px 12px;
    border: 1px solid var(--edge);
    border-radius: 4px;
    cursor: pointer;
    white-space: nowrap;
}
/* The tint is laid over whatever the button is filled with, so that hovering
   shows however a page has coloured its buttons. */
button:hover:not(:disabled) {
    border-color: currentColor;
    box-shadow: inset 0 0 0 999px color-mix(in srgb, currentColor 10%, transparent);
}
button:disabled { opacity: 0.5; cursor: default; }
.quiet { color: var(--muted); }
pre {
    margin: 0;
    padding: 1rem;
    border-top: 1px solid var(--edge);
    background: transparent;
    white-space: pre;
    overflow-x: auto;
}
pre:empty { display: none; }
.status {
    padding: 0.6rem 1rem;
    border-top: 1px solid var(--edge);
    font-size: 0.85em;
}
.status:empty { display: none; }
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
