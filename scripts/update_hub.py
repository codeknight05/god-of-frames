#!/usr/bin/env python3
"""Optional update-manifest server for God of Frames.

This server does not perform auto-updates itself.
It serves a version manifest that clients can query in future update checks.

Run:
  python scripts/update_hub.py

Environment:
  UPDATE_HUB_HOST=0.0.0.0
  UPDATE_HUB_PORT=8790
  UPDATE_HUB_MANIFEST=./data/update_manifest.json
  UPDATE_HUB_MAX_BODY_BYTES=32768
  UPDATE_HUB_VIEW_TOKEN=...   (optional)
  UPDATE_HUB_ADMIN_TOKEN=...  (optional, required for POST /api/version if set)
"""

from __future__ import annotations

import datetime as dt
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HOST = os.environ.get("UPDATE_HUB_HOST", "0.0.0.0")
PORT = int(os.environ.get("UPDATE_HUB_PORT", "8790"))
MANIFEST_PATH = os.environ.get(
    "UPDATE_HUB_MANIFEST",
    os.path.join(os.getcwd(), "data", "update_manifest.json"),
)
MAX_BODY_BYTES = int(os.environ.get("UPDATE_HUB_MAX_BODY_BYTES", "32768"))
VIEW_TOKEN = os.environ.get("UPDATE_HUB_VIEW_TOKEN", "").strip()
ADMIN_TOKEN = os.environ.get("UPDATE_HUB_ADMIN_TOKEN", "").strip()

_LOCK = threading.Lock()


def _utc_now() -> str:
    return dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def _default_manifest() -> dict:
    return {
        "app": "god_of_frames",
        "channel": "stable",
        "latest_version": "1.0.0",
        "min_supported_version": "1.0.0",
        "published_at_utc": _utc_now(),
        "download_url": "",
        "sha256": "",
        "release_notes_url": "",
        "notes": [
            "Initial update manifest. Set download_url and sha256 before enabling checks."
        ],
        "force_update": False,
    }


def _ensure_manifest_file() -> None:
    folder = os.path.dirname(MANIFEST_PATH)
    if folder:
        os.makedirs(folder, exist_ok=True)
    if os.path.exists(MANIFEST_PATH):
        return
    with open(MANIFEST_PATH, "w", encoding="utf-8") as f:
        json.dump(_default_manifest(), f, indent=2, ensure_ascii=True)


def _load_manifest() -> dict:
    _ensure_manifest_file()
    with _LOCK:
        with open(MANIFEST_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
    if not isinstance(data, dict):
        data = _default_manifest()
    merged = _default_manifest()
    merged.update(data)
    return merged


def _save_manifest(manifest: dict) -> None:
    folder = os.path.dirname(MANIFEST_PATH)
    if folder:
        os.makedirs(folder, exist_ok=True)
    with _LOCK:
        with open(MANIFEST_PATH, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=True)
            f.write("\n")


def _html_escape(v: object) -> str:
    return (
        str(v)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def _extract_query_token(parsed) -> str:
    return parse_qs(parsed.query).get("token", [""])[0].strip()


def _is_authorized(expected_token: str, header_token: str, query_token: str) -> bool:
    if not expected_token:
        return True
    candidate = (header_token or query_token).strip()
    return candidate == expected_token


def _build_html(manifest: dict, parsed) -> str:
    token_hint = ""
    if VIEW_TOKEN:
        token_hint = (
            "<div class='sub'>View token is enabled. Open with "
            "<code>?token=YOUR_VIEW_TOKEN</code>.</div>"
        )

    token_q = _extract_query_token(parsed)
    api_link = "/api/version"
    if token_q:
        api_link += f"?token={_html_escape(token_q)}"

    notes = manifest.get("notes", [])
    if not isinstance(notes, list):
        notes = []
    notes_html = "".join(f"<li>{_html_escape(x)}</li>" for x in notes)
    if not notes_html:
        notes_html = "<li>No notes.</li>"

    pretty_manifest = _html_escape(json.dumps(manifest, indent=2))

    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <meta http-equiv="refresh" content="10" />
  <title>God of Frames Update Hub</title>
  <style>
    :root{{--bg:#0b1020;--card:#111a33;--line:#22345f;--text:#e9eefc;--muted:#95a6ce;--accent:#3aa3ff;}}
    *{{box-sizing:border-box}}
    body{{margin:0;background:radial-gradient(1200px 700px at 15% -20%,#1e2a50 0%,var(--bg) 60%);color:var(--text);font-family:Segoe UI,system-ui,sans-serif}}
    .wrap{{max-width:1000px;margin:24px auto;padding:0 16px}}
    h1{{margin:0 0 6px 0;font-size:26px}}
    .sub{{color:var(--muted);margin-bottom:12px}}
    .card{{background:linear-gradient(180deg,#152348 0%,var(--card) 100%);border:1px solid var(--line);border-radius:12px;padding:12px;margin-bottom:10px}}
    .k{{font-size:12px;color:var(--muted);text-transform:uppercase}}
    .v{{font-size:20px;font-weight:700}}
    ul{{margin:8px 0 0 18px}}
    pre{{background:#0f1830;border:1px solid #274175;border-radius:8px;padding:10px;overflow:auto}}
    code{{background:#0f1830;border:1px solid #274175;padding:2px 6px;border-radius:8px}}
    a{{color:var(--accent)}}
  </style>
</head>
<body>
  <div class="wrap">
    <h1>God of Frames Update Hub</h1>
    <div class="sub">Serves update manifest for future client update checks.</div>
    {token_hint}
    <div class="sub">Manifest API: <a href="{api_link}">{api_link}</a></div>

    <div class="card">
      <div class="k">Latest Version</div>
      <div class="v">{_html_escape(manifest.get("latest_version", ""))}</div>
    </div>
    <div class="card">
      <div class="k">Download URL</div>
      <div class="v" style="font-size:14px">{_html_escape(manifest.get("download_url", ""))}</div>
    </div>
    <div class="card">
      <div class="k">SHA256</div>
      <div class="v" style="font-size:14px">{_html_escape(manifest.get("sha256", ""))}</div>
    </div>
    <div class="card">
      <div class="k">Notes</div>
      <ul>{notes_html}</ul>
    </div>
    <div class="card">
      <div class="k">Manifest JSON</div>
      <pre>{pretty_manifest}</pre>
    </div>
  </div>
</body>
</html>"""


class Handler(BaseHTTPRequestHandler):
    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-View-Token, X-Admin-Token")
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
            self._send_json(200, {"ok": True, "service": "update_hub", "utc": _utc_now()})
            return

        if path == "/api/version":
            if not _is_authorized(VIEW_TOKEN, view_header, query_token):
                self._unauthorized()
                return
            self._send_json(200, _load_manifest())
            return

        if path == "/":
            if not _is_authorized(VIEW_TOKEN, view_header, query_token):
                self._unauthorized()
                return
            html = _build_html(_load_manifest(), parsed).encode("utf-8")
            self._send(200, html, "text/html; charset=utf-8")
            return

        self._send(404, b"Not Found", "text/plain; charset=utf-8")

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path != "/api/version":
            self._send(404, b"Not Found", "text/plain; charset=utf-8")
            return

        query_token = _extract_query_token(parsed)
        admin_header = self.headers.get("X-Admin-Token", "")
        if not _is_authorized(ADMIN_TOKEN, admin_header, query_token):
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
            data = json.loads(raw.decode("utf-8"))
        except Exception:
            self._send_json(400, {"ok": False, "error": "invalid_json"})
            return

        if not isinstance(data, dict):
            self._send_json(400, {"ok": False, "error": "invalid_manifest"})
            return

        manifest = _default_manifest()
        manifest.update(data)
        manifest["published_at_utc"] = str(manifest.get("published_at_utc") or _utc_now())
        _save_manifest(manifest)
        self._send_json(200, {"ok": True, "saved": True})

    def log_message(self, fmt: str, *args) -> None:
        return


def main() -> None:
    _ensure_manifest_file()
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Update hub running on http://{HOST}:{PORT}")
    print(f"Manifest file: {MANIFEST_PATH}")
    print(f"View token enabled: {'yes' if VIEW_TOKEN else 'no'}")
    print(f"Admin token enabled: {'yes' if ADMIN_TOKEN else 'no'}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()

