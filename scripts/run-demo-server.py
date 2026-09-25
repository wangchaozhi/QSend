#!/usr/bin/env python3
"""Local HTTP echo server for QSend. Python 3.9+, standard library only."""
import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle_request(self):
        parts = urlsplit(self.path)
        if parts.path.startswith("/delay/"):
            try:
                time.sleep(min(30, max(0, float(parts.path.rsplit("/", 1)[1]))))
            except ValueError:
                pass
        length = int(self.headers.get("Content-Length", "0"))
        if length > 10 * 1024 * 1024:
            self.send_error(413)
            return
        raw = self.rfile.read(length).decode("utf-8", errors="replace")
        try:
            body = json.loads(raw) if raw else None
        except json.JSONDecodeError:
            body = raw
        status = 200
        if parts.path.startswith("/status/"):
            try:
                status = int(parts.path.rsplit("/", 1)[1])
                if status < 200 or status > 599:
                    status = 400
            except ValueError:
                status = 400
        response = {
            "message": "Hello from QSend",
            "method": self.command,
            "path": parts.path,
            "args": parse_qs(parts.query, keep_blank_values=True),
            "json": body,
            "headers": dict(self.headers),
            "ready": True,
        }
        encoded = json.dumps(response, ensure_ascii=False, indent=2).encode("utf-8")
        if status in (204, 304):
            encoded = b""
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("X-Powered-By", "QSend local demo")
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(encoded)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = do_OPTIONS = handle_request


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"QSend demo server: http://127.0.0.1:{args.port} (Ctrl+C to stop)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
