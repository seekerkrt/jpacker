#!/usr/bin/env python3

import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse


fixture_path = Path(sys.argv[1])
port_path = Path(sys.argv[2])
fixture = json.loads(fixture_path.read_text(encoding="utf-8"))


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)

        if "/v5/search/" in parsed.path:
            dependency = unquote(parsed.path.rsplit("/", 1)[-1])
            names = fixture.get("providers", {}).get(dependency, [])
        else:
            names = query.get("arg[]", [])

        packages = fixture.get("packages", {})
        results = [packages[name] for name in names if name in packages]
        response = {
            "version": 5,
            "type": "multiinfo",
            "resultcount": len(results),
            "results": results,
        }
        body = json.dumps(response).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        return


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
port_path.write_text(str(server.server_port), encoding="utf-8")
server.serve_forever()
