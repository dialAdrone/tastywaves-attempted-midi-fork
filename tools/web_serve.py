# SPDX-License-Identifier: GPL-3.0-only
# Serves tools/web on 127.0.0.1 with caching off, so a reload always gets the current page and scripts (tools/web_tool.sh).
#   python3 tools/web_serve.py PORT
import functools, http.server, os, sys

class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
web = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'web')
http.server.ThreadingHTTPServer(('127.0.0.1', port), functools.partial(NoCache, directory=web)).serve_forever()
