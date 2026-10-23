#!/usr/bin/env python3
#
# CLAUDE: Serve the documentation site for development.
#
#   python docs/serve.py [--dir <directory>] [--port <port>]
#
# --dir is the site to serve (default build/site); see docs/build.py.
# --port is the port to listen on (default 8000).
#
# Each response tells the browser to check for a newer file before using a cached one.
# Without this, a browser may keep using an old atomweb.wasm or atom-worker.js after
# a rebuild, since a reload does not reliably refetch the files a worker loads.

import argparse
import functools
import http.server
import os

DOCS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(DOCS)


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description="Serve the documentation site for development.")
    parser.add_argument("--dir", default=os.path.join(REPO, "build", "site"),
        help="the site to serve")
    parser.add_argument("--port", type=int, default=8000,
        help="the port to listen on")
    args = parser.parse_args()

    handler = functools.partial(NoCacheHandler, directory=os.path.abspath(args.dir))
    server = http.server.ThreadingHTTPServer(("", args.port), handler)
    print(f"Serving {args.dir} at http://localhost:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
