#!/usr/bin/env python3

import functools
import ssl
import sys
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


if len(sys.argv) != 7:
    raise SystemExit(
        "usage: git_remote_revision_https_server.py ROOT CERT KEY PORT LOG HOST"
    )

fixture_root = Path(sys.argv[1])
certificate_path = Path(sys.argv[2])
private_key_path = Path(sys.argv[3])
port_path = Path(sys.argv[4])
request_log_path = Path(sys.argv[5])
fixture_host = sys.argv[6]
request_log_lock = threading.Lock()


class FixtureServer(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        # A Git client may close a dumb-HTTP response as soon as it has enough
        # bytes. That transport detail is not a fixture failure or diagnostic.
        return


class Handler(SimpleHTTPRequestHandler):
    def record_request(self):
        authorization_present = int(self.headers.get("Authorization") is not None)
        cookie_present = int(self.headers.get("Cookie") is not None)
        with request_log_lock:
            with request_log_path.open("a", encoding="utf-8") as request_log:
                request_log.write(
                    f"path={self.path} authorization={authorization_present} "
                    f"cookie={cookie_present}\n"
                )

    def do_GET(self):
        self.record_request()
        request_path = urlsplit(self.path).path
        if request_path == "/redirect.git" or request_path.startswith(
            "/redirect.git/"
        ):
            suffix = request_path[len("/redirect.git") :]
            location = (
                f"https://{fixture_host}:{self.server.server_port}"
                f"/sha1.git{suffix}"
            )
            self.send_response(302)
            self.send_header("Location", location)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if request_path == "/auth.git" or request_path.startswith("/auth.git/"):
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="observer-fixture"')
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        super().do_GET()

    def end_headers(self):
        # A malicious ambient cookie policy would persist this value. The
        # production observer resets cookieFile/saveCookies, and the sentinel
        # verifies that its watched marker stays byte-identical.
        self.send_header("Set-Cookie", "observer_fixture=present; Path=/")
        super().end_headers()

    def log_message(self, format, *args):
        return


handler = functools.partial(Handler, directory=str(fixture_root))
server = FixtureServer(("127.0.0.1", 0), handler)
tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
tls_context.load_cert_chain(certificate_path, private_key_path)
server.socket = tls_context.wrap_socket(server.socket, server_side=True)
request_log_path.write_text("", encoding="utf-8")
port_path.write_text(str(server.server_port), encoding="utf-8")
server.serve_forever()
