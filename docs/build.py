#!/usr/bin/env python3
#
# CLAUDE: Build the documentation site from the markdown pages in docs/.
#
#   python docs/build.py [--atom <directory>] [--out <directory>]
#
# --atom is the built web front end (default build/wasm/web); see web/publish.sh.
# --out is where the site is written (default build/site). Serve that directory over
# http to view it, e.g. python -m http.server -d build/site, since a browser does
# not run the web front end from a file:// address.

import argparse
import html
import os
import re
import shutil
import sys

import mistune

DOCS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(DOCS)
# CLAUDE: the data files that snippets read with :load, copied to the same place in the site
DATA = os.path.join(DOCS, "data")

# CLAUDE: each published page, from the markdown file name in docs/ to the page written
PAGES = {
    "Atom tutorial.md": "index.html",
}

# CLAUDE: the files of the web front end that a page needs; see web/publish.sh
ATOM_FILES = ["atomweb.wasm", "atomweb.mjs", "atom-snippet.js", "atom-session.js", "atom-worker.js"]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def extract_meta(text, key):
    m = re.search(rf"<!--\s*{key}:\s*(.+?)\s*-->", text)
    return m.group(1) if m else ""


def strip_meta(text):
    return re.sub(r"<!--\s*\w+:\s*.+?\s*-->\n?", "", text)


def heading_anchor(text):
    # CLAUDE: the anchor of a heading, from the heading text as Obsidian links to it
    text = re.sub(r"<[^>]+>", "", html.unescape(text)).strip().lower()
    text = re.sub(r"[^\w\s-]", "", text)
    return re.sub(r"\s+", "-", text)


class AnchorRenderer(mistune.HTMLRenderer):
    # CLAUDE: give each heading an id, so that a link can name the heading

    def __init__(self):
        super().__init__(escape=False)
        self.anchors = set()

    def heading(self, text, level, **attrs):
        anchor = heading_anchor(text)
        unique = anchor
        n = 1
        while unique in self.anchors:
            n += 1
            unique = f"{anchor}-{n}"
        self.anchors.add(unique)
        return f'<h{level} id="{unique}">{text}</h{level}>\n'

    def link(self, text, url, title=None):
        # CLAUDE: a link to another site opens in a new tab, so that the tutorial session is kept
        a = super().link(text, url, title)
        if re.match(r"(https?:)?//", url):
            a = a.replace("<a ", '<a target="_blank" rel="noopener" ', 1)
        return a


def wiki_link(match, page_name, warnings):
    # CLAUDE: an Obsidian link [[page#heading|text]] as an html link. The page and the
    # heading may each be left out. A page that is not published is left as plain text.
    target, _, text = match.group(1).partition("|")
    page, _, heading = target.partition("#")
    page = page.strip() or page_name
    text = text.strip() or heading.strip() or page
    source = page + ".md"
    if source not in PAGES:
        warnings.append(f"[[{match.group(1)}]] links to {page}, which is not published")
        return text
    href = "" if page == page_name else PAGES[source]
    if heading:
        href += "#" + heading_anchor(heading)
    return f'<a href="{href}">{text}</a>'


def convert_wiki_links(content, page_name, warnings):
    # CLAUDE: code blocks and inline code are left alone, since [[ is also atom syntax
    parts = re.split(r"(<pre.*?</pre>|<code.*?</code>)", content, flags=re.DOTALL)
    for i in range(0, len(parts), 2):
        parts[i] = re.sub(
            r"\[\[([^\]]+)\]\]",
            lambda m: wiki_link(m, page_name, warnings),
            parts[i])
    return "".join(parts)


def enable_atom_snippets(content):
    # CLAUDE: a code block tagged atom runs in the page. The code block stays inside
    # the atom-snippet element, so a reader without javascript still sees the code.
    return re.sub(
        r'<pre><code class="language-atom">.*?</code></pre>',
        lambda m: f"<atom-snippet>{m.group(0)}</atom-snippet>",
        content,
        flags=re.DOTALL,
    )


def data_files():
    # CLAUDE: the path of each file in DATA, relative to docs/, with / as separator
    paths = []
    for directory, _, names in os.walk(DATA):
        for name in names:
            path = os.path.relpath(os.path.join(directory, name), DOCS)
            paths.append(path.replace(os.sep, "/"))
    return sorted(paths)


def build_page(source, template, warnings):
    raw = read(os.path.join(DOCS, source))
    page_name = source[: -len(".md")]
    title = extract_meta(raw, "title") or page_name
    markdown = mistune.create_markdown(
        renderer=AnchorRenderer(),
        plugins=["strikethrough", "footnotes", "table"])
    content = markdown(strip_meta(raw))
    content = convert_wiki_links(content, page_name, warnings)
    content = enable_atom_snippets(content)
    content = f"<h1>{title}</h1>\n" + content

    head = ""
    scripts = ""
    if "<atom-snippet>" in content:
        scripts = '<script type="module" src="atom/atom-snippet.js"></script>'
        # CLAUDE: the session writes these files to its file system; see web/atom-session.js
        files = data_files()
        if files:
            head = f'<meta name="atom-files" content="{html.escape(" ".join(files))}">'
    page = template.replace("{{title}}", title)
    page = page.replace("{{head}}", head)
    page = page.replace("{{content}}", content)
    page = page.replace("{{scripts}}", scripts)
    return page


def main():
    parser = argparse.ArgumentParser(description="Build the documentation site.")
    parser.add_argument("--atom", default=os.path.join(REPO, "build", "wasm", "web"),
        help="the built web front end")
    parser.add_argument("--out", default=os.path.join(REPO, "build", "site"),
        help="where the site is written")
    args = parser.parse_args()

    missing = [f for f in ATOM_FILES if not os.path.isfile(os.path.join(args.atom, f))]
    if missing:
        sys.exit(f"{args.atom} has no {', '.join(missing)}; build the web front end first:\n"
            "  emcmake cmake -B build/wasm -DCMAKE_BUILD_TYPE=Debug\n"
            "  cmake --build build/wasm")

    if os.path.exists(args.out):
        shutil.rmtree(args.out)

    template = read(os.path.join(DOCS, "template.html"))
    warnings = []
    for source, target in PAGES.items():
        write(os.path.join(args.out, target), build_page(source, template, warnings))

    shutil.copy2(os.path.join(DOCS, "styles.css"), os.path.join(args.out, "styles.css"))
    shutil.copy2(os.path.join(REPO, "atom-logo-no-background.svg"), os.path.join(args.out, "atom-logo.svg"))
    if os.path.isdir(DATA):
        shutil.copytree(DATA, os.path.join(args.out, "data"))
    os.makedirs(os.path.join(args.out, "atom"))
    for f in ATOM_FILES:
        shutil.copy2(os.path.join(args.atom, f), os.path.join(args.out, "atom", f))

    for warning in warnings:
        print("warning:", warning, file=sys.stderr)
    print(f"Built {len(PAGES)} page(s) in {args.out}")


if __name__ == "__main__":
    main()
