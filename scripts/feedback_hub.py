#!/usr/bin/env python3
"""Centralized feedback relay + dashboard for God of Frames.

Run:
  python scripts/feedback_hub.py

Environment:
  FEEDBACK_HUB_HOST=0.0.0.0
  FEEDBACK_HUB_PORT=8787
  FEEDBACK_HUB_DATA=./data/feedback_hub.jsonl
  FEEDBACK_HUB_MAX_STORE_ITEMS=5000
  FEEDBACK_HUB_MAX_RESPONSE_ITEMS=500
  FEEDBACK_HUB_MAX_MESSAGE_LEN=2000
  FEEDBACK_HUB_MAX_BODY_BYTES=16384
  FEEDBACK_HUB_INGEST_TOKEN=your_secret_ingest_token    (optional)
  FEEDBACK_HUB_VIEW_TOKEN=your_secret_view_token        (optional)

Auth model:
- If FEEDBACK_HUB_INGEST_TOKEN is set, POST /api/ingest requires token.
- If FEEDBACK_HUB_VIEW_TOKEN is set, GET dashboard/API requires token.
- Token can be sent via query (?token=...) or header.
"""

from __future__ import annotations

import datetime as dt
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HOST = os.environ.get("FEEDBACK_HUB_HOST", "0.0.0.0")
PORT = int(os.environ.get("FEEDBACK_HUB_PORT", "8787"))
DATA_PATH = os.environ.get(
    "FEEDBACK_HUB_DATA",
    os.path.join(os.getcwd(), "data", "feedback_hub.jsonl"),
)
MAX_STORE_ITEMS = int(os.environ.get("FEEDBACK_HUB_MAX_STORE_ITEMS", "5000"))
MAX_RESPONSE_ITEMS = int(os.environ.get("FEEDBACK_HUB_MAX_RESPONSE_ITEMS", "500"))
MAX_MESSAGE_LEN = int(os.environ.get("FEEDBACK_HUB_MAX_MESSAGE_LEN", "2000"))
MAX_BODY_BYTES = int(os.environ.get("FEEDBACK_HUB_MAX_BODY_BYTES", "16384"))
INGEST_TOKEN = os.environ.get("FEEDBACK_HUB_INGEST_TOKEN", "").strip()
VIEW_TOKEN = os.environ.get("FEEDBACK_HUB_VIEW_TOKEN", "").strip()

_LOCK = threading.Lock()


def _utc_now() -> str:
    return dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def _ensure_data_dir() -> None:
    folder = os.path.dirname(DATA_PATH)
    if folder:
        os.makedirs(folder, exist_ok=True)


def _clean_field(value: object, max_len: int = 512) -> str:
    s = str(value or "")
    s = s.replace("\r", " ").replace("\n", " ").replace("\t", " ").strip()
    if len(s) > max_len:
        s = s[:max_len]
    return s


def _trim_storage_if_needed() -> None:
    if not os.path.exists(DATA_PATH):
        return

    with _LOCK:
        with open(DATA_PATH, "r", encoding="utf-8") as f:
            lines = f.readlines()
        if len(lines) <= MAX_STORE_ITEMS:
            return
        with open(DATA_PATH, "w", encoding="utf-8") as f:
            f.writelines(lines[-MAX_STORE_ITEMS:])


def _append_feedback(item: dict) -> None:
    _ensure_data_dir()
    with _LOCK:
        with open(DATA_PATH, "a", encoding="utf-8") as f:
            f.write(json.dumps(item, ensure_ascii=True) + "\n")
    _trim_storage_if_needed()


def _load_feedback(limit: int = MAX_RESPONSE_ITEMS) -> list[dict]:
    if not os.path.exists(DATA_PATH):
        return []

    if limit < 1:
        limit = 1
    if limit > MAX_RESPONSE_ITEMS:
        limit = MAX_RESPONSE_ITEMS

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


def _html_escape(v: object) -> str:
    return (
        str(v)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def _build_html(items: list[dict], parsed) -> str:
    token_hint = ""
    if VIEW_TOKEN:
        token_hint = (
            "<div class='sub'>View token is enabled. Open dashboard with "
            "<code>?token=YOUR_VIEW_TOKEN</code>.</div>"
        )

    cards = []
    for item in reversed(items):
        timestamp = _html_escape(item.get("timestamp", "--"))
        category = _html_escape(item.get("category", "general"))
        game = _html_escape(item.get("game", "--"))
        contact = _html_escape(item.get("contact", ""))
        message = _html_escape(item.get("message", ""))
        received = _html_escape(item.get("received_utc", "--"))
        source_ip = _html_escape(item.get("source_ip", "--"))
        contact_html = f"<div class='meta'>contact: {contact}</div>" if contact else ""
        cards.append(
            "<div class='card'>"
            f"<div class='meta'>{timestamp} | {category} | {game}</div>"
            f"{contact_html}"
            f"<div class='msg'>{message}</div>"
            f"<div class='meta'>received: {received} | source_ip: {source_ip}</div>"
            "</div>"
        )

    if not cards:
        cards.append("<div class='card'><div class='meta'>No feedback yet.</div></div>")

    token_q = parse_qs(parsed.query).get("token", [""])[0]
    api_link = "/api/feedback"
    if token_q:
        api_link += f"?token={_html_escape(token_q)}"

    return f"""<!doctype html>
<html>
<head>
  <meta charset=\"utf-8\" />
  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\" />
  <meta http-equiv=\"refresh\" content=\"5\" />
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
    a{{color:var(--accent)}}
  </style>
</head>
<body>
  <div class=\"wrap\">
    <h1>God of Frames Feedback Hub</h1>
    <div class=\"sub\">Auto-refresh every 5s. Client endpoint: <code>http://YOUR_HOST:{PORT}/api/ingest</code></div>
    {token_hint}
    <div class=\"sub\">API: <a href=\"{api_link}\">{api_link}</a></div>
    {''.join(cards)}
  </div>
</body>
</html>"""


def _extract_query_token(parsed) -> str:
    return parse_qs(parsed.query).get("token", [""])[0].strip()


def _is_authorized(expected_token: str, header_token: str, query_token: str) -> bool:
    if not expected_token:
        return True
    candidate = (header_token or query_token).strip()
    return candidate == expected_token


class Handler(BaseHTTPRequestHandler):
    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-Feedback-Token, X-View-Token")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, status: int, obj: dict) -> None:
        data = json.dumps(obj, ensure_ascii=True).encode("utf-8")
        self._send(status, data, "application/json; charset=utf-8")

    def _unauthorized(self) -> None:
        self._send_json(401, {"ok": False, "error": "unauthorized"})

    def do_OPTIONS(self) -> None:
        self._send(204, b"", "text/plain; charset=utf-8")

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        query_token = _extract_query_token(parsed)
        view_header = self.headers.get("X-View-Token", "")

        if path == "/healthz":
            self._send_json(200, {"ok": True, "service": "feedback_hub", "utc": _utc_now()})
            return

        if path == "/api/feedback":
            if not _is_authorized(VIEW_TOKEN, view_header, query_token):
                self._unauthorized()
                return

            limit = MAX_RESPONSE_ITEMS
            try:
                q_limit = parse_qs(parsed.query).get("limit", [""])[0]
                if q_limit:
                    limit = int(q_limit)
            except ValueError:
                limit = MAX_RESPONSE_ITEMS

            items = list(reversed(_load_feedback(limit=limit)))
            self._send_json(200, {"items": items})
            return

        if path == "/api/stats":
            if not _is_authorized(VIEW_TOKEN, view_header, query_token):
                self._unauthorized()
                return

            items = _load_feedback(limit=MAX_RESPONSE_ITEMS)
            categories: dict[str, int] = {}
            for item in items:
                cat = str(item.get("category", "general") or "general")
                categories[cat] = categories.get(cat, 0) + 1

            self._send_json(
                200,
                {
                    "ok": True,
                    "recent_items": len(items),
                    "categories": categories,
                    "max_response_items": MAX_RESPONSE_ITEMS,
                },
            )
            return

        if path == "/":
            if not _is_authorized(VIEW_TOKEN, view_header, query_token):
                self._unauthorized()
                return
            html = _build_html(_load_feedback(limit=MAX_RESPONSE_ITEMS), parsed).encode("utf-8")
            self._send(200, html, "text/html; charset=utf-8")
            return

        self._send(404, b"Not Found", "text/plain; charset=utf-8")

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path

        if path != "/api/ingest":
            self._send(404, b"Not Found", "text/plain; charset=utf-8")
            return

        query_token = _extract_query_token(parsed)
        ingest_header = self.headers.get("X-Feedback-Token", "")
        if not _is_authorized(INGEST_TOKEN, ingest_header, query_token):
            self._unauthorized()
            return

        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            size = 0

        if size <= 0:
            self._send_json(400, {"ok": False, "error": "missing_body"})
            return
        if size > MAX_BODY_BYTES:
            self._send_json(413, {"ok": False, "error": "payload_too_large"})
            return

        raw = self.rfile.read(size)

        try:
            payload = json.loads(raw.decode("utf-8"))
        except Exception:
            self._send_json(400, {"ok": False, "error": "invalid_json"})
            return

        message = _clean_field(payload.get("message", ""), MAX_MESSAGE_LEN)
        if not message:
            self._send_json(400, {"ok": False, "error": "missing_message"})
            return

        item = {
            "timestamp": _clean_field(payload.get("timestamp", ""), 64) or _utc_now(),
            "category": _clean_field(payload.get("category", "general"), 64) or "general",
            "game": _clean_field(payload.get("game", ""), 160),
            "contact": _clean_field(payload.get("contact", ""), 256),
            "message": message,
            "received_utc": _utc_now(),
            "source": "god_of_frames_client",
            "source_ip": self.client_address[0] if self.client_address else "",
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
    print(f"Ingest token enabled: {'yes' if INGEST_TOKEN else 'no'}")
    print(f"View token enabled: {'yes' if VIEW_TOKEN else 'no'}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
