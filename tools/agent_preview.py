"""Serve the round badge on loopback, following JSONL on stdin or replaying an explicit file."""
import argparse
import json
import math
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import threading
import time
from urllib.parse import unquote, urlsplit

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "host"))
from baji_bridge.preview import PreviewState
from baji_bridge.protocol import PREFIX, decode


def read_stream(stream, state, pause=0):
    # Raw CLI items may contain long command output. Only event and item types enter the view.
    first = True
    try:
        for line in stream:
            if not line.strip():
                continue
            try:
                msg = decode(line) if line.startswith(PREFIX) else json.loads(line.decode("utf-8-sig"))
            except (ValueError, UnicodeError, RecursionError):
                continue
            if pause and not first:
                time.sleep(pause)
            if state.accept(msg):
                first = False
    finally:
        state.close_input()


def public_file(url):
    if url.startswith("/badge/assets/"):
        url = url.removeprefix("/badge")
    if url.startswith("/badge/design/"):
        root, name = PROJECT / "design", url.removeprefix("/badge/design/")
    elif url.startswith("/assets/ai-girls/state-poses/"):
        root, name = PROJECT / "assets/ai-girls/state-poses", url.removeprefix("/assets/ai-girls/state-poses/")
    elif url == "/assets/ai-girls/sources/crosspet/LICENSE.md":
        return PROJECT / "assets/ai-girls/sources/crosspet/LICENSE.md"
    else:
        return None
    path = (root / name).resolve()
    return path if path.is_relative_to(root.resolve()) and path.is_file() else None


class BadgeHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        route = unquote(urlsplit(self.path).path)
        if route == "/":
            self.send_response(302)
            self.send_header("Location", "/badge/design/badge-prototype.html?agent=1")
            self.end_headers()
            return
        if route == "/agent/state":
            data = json.dumps(self.server.state.view(), ensure_ascii=False).encode()
            mime = "application/json; charset=utf-8"
        else:
            path = public_file(route)
            if not path:
                self.send_error(404)
                return
            data = path.read_bytes()
            mime = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
            if path.suffix in (".md", ".html", ".js", ".css"):
                mime += "; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--model", help="The model explicitly selected for the incoming Codex exec run")
    parser.add_argument("--replay", type=Path, help="Replay a local JSONL fixture instead of stdin")
    parser.add_argument("--step", type=float, default=4, help="Seconds per replay line")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or not math.isfinite(args.step) or args.step <= 0:
        parser.error("Use a valid port and a positive replay interval")
    if args.replay and not args.replay.is_file():
        parser.error("The replay file does not exist")
    state = PreviewState(model=args.model, replay=args.replay is not None)
    with ThreadingHTTPServer(("127.0.0.1", args.port), BadgeHandler) as server:
        server.state = state
        print(f"http://127.0.0.1:{server.server_port}/", file=sys.stderr, flush=True)
        def consume():
            if args.replay:
                with args.replay.open("rb") as stream:
                    read_stream(stream, state, args.step)
            else:
                read_stream(sys.stdin.buffer, state)
        threading.Thread(target=consume, daemon=True).start()
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
