#!/usr/bin/env python3

import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse


fixture_path = Path(sys.argv[1])
port_path = Path(sys.argv[2])
fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
info_sequence_group_counts = {}
info_sequence_group_lock = threading.Lock()


def sequenced_info_results(requested_name, packages):
    for group_name, group in fixture.get("info_sequence_groups", {}).items():
        if requested_name not in group.get("requests", []):
            continue

        with info_sequence_group_lock:
            request_index = info_sequence_group_counts.get(group_name, 0)
            info_sequence_group_counts[group_name] = request_index + 1

        result_overrides = group.get("result_overrides", [])
        if request_index >= len(result_overrides):
            return None
        result_override = result_overrides[request_index]
        if result_override is None:
            return None

        package = packages.get(requested_name)
        if not isinstance(package, dict) or not isinstance(result_override, dict):
            return None
        result = package.copy()
        result.update(result_override)
        return [result]
    return None


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        sequenced_results = None

        if "/v5/search/" in parsed.path:
            dependency = unquote(parsed.path.rsplit("/", 1)[-1])
            names = fixture.get("providers", {}).get(dependency, [])
        else:
            names = query.get("arg[]", [])
            if len(names) == 1:
                requested_name = names[0]
                sequenced_results = sequenced_info_results(
                    requested_name, fixture.get("packages", {})
                )
                if sequenced_results is None:
                    names = fixture.get("info_results", {}).get(requested_name, names)

        packages = fixture.get("packages", {})
        if sequenced_results is not None:
            results = sequenced_results
        else:
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
