#!/usr/bin/env python3
"""Lightweight feedback relay + viewer for God of Frames.

Run:
  python scripts/feedback_hub.py

Default bind:
  http://0.0.0.0:8787
"""

from __future__ import annotations

import datetime as dt
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

HOST = os.environ.get("FEEDBACK_HUB_HOST", "0.0.0.0")
PORT = int(os.environ.get("FEEDBACK_HUB_PORT", "8787"))
DATA_PATH = os.environ.get(
    "FEEDBACK_HUB_DATA",
    os.path.join(os.getcwd(), "data", "feedback_hub.jsonl"),
)
MAX_FEEDBACK_ITEMS = 500

_LOCK = threading.Lock()


def _utc_now() -> str:
    return dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def _ensure_data_dir() -> None:
    folder = os.path.dirname(DATA_PATH)
    if folder:
        os.makedirs(folder, exist_ok=True)


def _append_feedback(item: dict) -> None:
    _ensure_data_dir()
    with _LOCK:
        with open(DATA_PATH, "a", encoding="utf-8") as f:
            f.write(json.dumps(item, ensure_ascii=True) + "\n")


def _load_feedback(limit: int = MAX_FEEDBACK_ITEMS) -> list[dict]:
    if not os.path.exists(DATA_PATH):
        return []

    rows: list[dict] = []
    with _LOCK:
        with open(DATA_PATH, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(row, dict):
                    rows.append(row)

    return rows[-limit:]


def _html_escape(v: str) -> str:
    return (
        str(v)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def _build_html(items: list[dict]) -> str:
    cards = []
    for item in reversed(items):
        timestamp = _html_escape(item.get("timestamp", "--"))
        category = _html_escape(item.get("category", "general"))
        game = _html_escape(item.get("game", "--"))
        contact = _html_escape(item.get("contact", ""))
        message = _html_escape(item.get("message", ""))
        received = _html_escape(item.get("received_utc", "--"))
        contact_html = f"<div class='meta'>contact: {contact}</div>" if contact else ""
        cards.append(
            "<div class='card'>"
            f"<div class='meta'>{timestamp} | {category} | {game}</div>"
            f"{contact_html}"
            f"<div class='msg'>{message}</div>"
            f"<div class='meta'>received: {received}</div>"
            "</div>"
        )

    if not cards:
        cards.append("<div class='card'><div class='meta'>No feedback yet.</div></div>")

    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <meta http-equiv="refresh" content="5" />
  <title>God of Frames Feedback Hub</title>
  <style>
    :root{{--bg:#0b1020;--card:#111a33;--line:#22345f;--text:#e9eefc;--muted:#95a6ce;--accent:#3aa3ff;}}
    *{{box-sizing:border-box}}
    body{{margin:0;background:radial-gradient(1200px 700px at 15% -20%,#1e2a50 0%,var(--bg) 60%);color:var(--text);font-family:Segoe UI,system-ui,sans-serif}}
    .wrap{{max-width:1000px;margin:24px auto;padding:0 16px}}
    h1{{margin:0 0 6px 0;font-size:26px}}
    .sub{{color:var(--muted);margin-bottom:14px}}
    .card{{background:linear-gradient(180deg,#152348 0%,var(--card) 100%);border:1px solid var(--line);border-radius:12px;padding:12px;margin-bottom:10px}}
    .meta{{font-size:12px;color:var(--muted);margin-bottom:6px}}
    .msg{{font-size:15px;line-height:1.35}}
    code{{background:#0f1830;border:1px solid #274175;padding:2px 6px;border-radius:8px}}
  </style>
</head>
<body>
  <div class="wrap">
    <h1>God of Frames Feedback Hub</h1>
    <div class="sub">Auto-refresh every 5s. Configure clients with: <code>feedback_endpoint=http://YOUR_HOST:{PORT}/api/ingest</code></div>
    {''.join(cards)}
  </div>
</body>
</html>"""


class Handler(BaseHTTPRequestHandler):
    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, status: int, obj: dict) -> None:
        data = json.dumps(obj, ensure_ascii=True).encode("utf-8")
        self._send(status, data, "application/json; charset=utf-8")

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/feedback":
            self._send_json(200, {"items": list(reversed(_load_feedback()))})
            return
        if path == "/":
            html = _build_html(_load_feedback()).encode("utf-8")
            self._send(200, html, "text/html; charset=utf-8")
            return
        self._send(404, b"Not Found", "text/plain; charset=utf-8")

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        if path != "/api/ingest":
            self._send(404, b"Not Found", "text/plain; charset=utf-8")
            return

        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            size = 0
        raw = self.rfile.read(size) if size > 0 else b"{}"

        try:
            payload = json.loads(raw.decode("utf-8"))
        except Exception:
            self._send_json(400, {"ok": False, "error": "invalid_json"})
            return

        message = str(payload.get("message", "")).strip()
        if not message:
            self._send_json(400, {"ok": False, "error": "missing_message"})
            return

        item = {
            "timestamp": str(payload.get("timestamp", "")).strip() or _utc_now(),
            "category": str(payload.get("category", "general")).strip() or "general",
            "game": str(payload.get("game", "")).strip(),
            "contact": str(payload.get("contact", "")).strip(),
            "message": message[:2000],
            "received_utc": _utc_now(),
            "source": "god_of_frames_client",
        }
        _append_feedback(item)

        self._send_json(200, {"ok": True})

    def log_message(self, fmt: str, *args) -> None:
        return


def main() -> None:
    _ensure_data_dir()
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Feedback hub running on http://{HOST}:{PORT}")
    print(f"Data file: {DATA_PATH}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()


