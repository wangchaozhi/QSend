"""Exercise the deployed app with the Qt SDK hidden, local HTTP, and TLS probes."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Echo(BaseHTTPRequestHandler):
    def do_GET(self):
        payload = b'{"releaseSmoke":true}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--platform", required=True, choices=["windows-x64", "macos-universal", "linux-x64"])
    parser.add_argument("--qt-root", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--isolate-qt", action="store_true", help="CI only: temporarily rename SDK directory")
    args = parser.parse_args()
    stage, qt_root = args.stage.resolve(), args.qt_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    command = {
        "windows-x64": [str(stage / "QSend.exe")],
        "macos-universal": [str(stage / "QSend.app/Contents/MacOS/QSend")],
        "linux-x64": [str(stage / "run-qsend.sh")],
    }[args.platform]
    assert Path(command[0]).is_file(), command
    env = os.environ.copy()
    for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
        env.pop(key, None)
    env["PATH"] = os.pathsep.join(p for p in env.get("PATH", "").split(os.pathsep)
                                if str(qt_root).lower() not in p.lower())
    # The Windows offscreen backend cannot find system fonts. Exercise the
    # shipped native plugin so the screenshot also checks real text rendering.
    env["QT_QPA_PLATFORM"] = "windows" if args.platform == "windows-x64" else "offscreen"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    hidden = qt_root.with_name(qt_root.name + "-qsend-ci-hidden")
    renamed = False
    server = ThreadingHTTPServer(("127.0.0.1", 0), Echo)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        if args.isolate_qt:
            assert os.environ.get("CI") == "true", "SDK isolation is only allowed in CI"
            assert not hidden.exists() and qt_root.is_dir()
            assert qt_root not in stage.parents and stage != qt_root
            qt_root.rename(hidden)
            renamed = True
        with tempfile.TemporaryDirectory(prefix="qsend-smoke-") as scratch:
            workspace = Path(scratch) / "workspace.json"
            runtime = (args.output / "runtime.json").resolve()
            capture = (args.output / "package-smoke.png").resolve()
            def run(extra):
                result = subprocess.run(command + extra, env=env, cwd=scratch,
                                        capture_output=True, text=True, timeout=30)
                print(result.stdout)
                print(result.stderr)
                assert result.returncode == 0, f"Packaged app exited {result.returncode}"
            run(["--runtime-info", str(runtime)])
            details = json.loads(runtime.read_text(encoding="utf-8"))
            assert details["version"] == args.version, details
            assert details["sslSupported"] and details["sslBackend"], details
            run(["--workspace", str(workspace), "--url", f"http://127.0.0.1:{server.server_port}/smoke",
                 "--send", "--screenshot", str(capture), "--capture-delay", "2500"])
            data = json.loads(workspace.read_text(encoding="utf-8"))
            assert data["history"][0]["statusCode"] == 200, data["history"]
            assert capture.stat().st_size > 1000
            print(f"PASS: {args.platform}, version {args.version}, TLS {details['sslBackend']}, HTTP 200")
    finally:
        if renamed:
            hidden.rename(qt_root)
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
