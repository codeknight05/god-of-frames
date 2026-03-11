# God of Frames

God of Frames is a Windows C++ game performance watchdog that detects FPS drops, analyzes likely bottlenecks, applies safe automatic fixes, and learns from previous optimization outcomes.

It is designed as a practical alternative to traditional monitoring tools: always-on watch mode, in-game style overlay, local control website, and policy-based remediation.

Recent control-plane improvements borrow a few useful ideas from `eventstream`: bounded in-memory task queues, a small worker pool for request handling, and built-in health/stats endpoints.

## Key Features

- Real-time telemetry for:
  - FPS (via PresentMon)
  - game CPU usage
  - game memory usage
  - system memory pressure
- Auto game detect mode (`--watch-games`) that attaches when configured games start.
- Bottleneck analysis from:
  - local deterministic rules
  - optional Gemini suggestions (if `GEMINI_API_KEY` is set)
- Safe auto-fix engine with allow-listed actions only.
- Learning loop that scores past actions and prioritizes what worked.
- Built-in local control web UI (`http://127.0.0.1:5055`) to change runtime settings.
- Async local control server with bounded request queue and worker pool.
- Feedback tab with optional relay endpoint for cross-user product feedback.
- Optional update-manifest URL setting for future auto-update checks.
- In-game style overlay with hotkey cycle:
  - full overlay
  - mini FPS corner
  - hidden
- Severe FPS drop reason banner shown for 5 seconds.

## Current Auto-Fix Scope

Current automatic actions are intentionally restricted:

- `SET_HIGH_PERF_POWER_PLAN`
- `SET_GAME_PRIORITY_HIGH`
- `TRIM_BACKGROUND_APPS`

Actions that require user judgment (driver updates, BIOS changes, graphics presets, etc.) are surfaced as notifications only.

## Architecture Overview

High-level runtime flow:

1. `SystemMonitor` captures current telemetry snapshot.
2. `BottleneckAnalyzer` proposes local actions.
3. `GeminiClient` (optional) proposes additional actions.
4. Action planner merges, deduplicates, and ranks actions using learning scores.
5. `Remediator` executes only safe allow-listed auto-fixes.
6. `HistoryStore` records telemetry + outcomes and updates learning scores.
7. `DashboardWriter` updates `data/dashboard.html`.
8. `OverlayManager` renders overlay and severe-drop alerts.
9. `ControlServer` serves UI/API and applies settings live.
10. Worker-pool backed request handling keeps the control UI responsive when feedback forwarding or file I/O is slow.

## Project Structure

```text
src/
  main.cpp
  system_monitor.*
  bottleneck_analyzer.*
  remediator.*
  history_store.*
  gemini_client.*
  overlay_manager.*
  control_server.*
  settings.*
  dashboard_writer.*

scripts/
  package_release.ps1
  feedback_hub.py
  update_hub.py

settings.example.conf
CMakeLists.txt
README.md
```

## Requirements

- Windows 10/11 (x64)
- CMake 3.16+
- Visual Studio Build Tools 2022 (MSVC C++)
- PresentMon available on system PATH (or installed via WinGet fallback path)
- Optional: Gemini API key (`GEMINI_API_KEY`) for AI-assisted suggestions
- Optional: Python 3 for running `scripts/feedback_hub.py` and `scripts/update_hub.py`

## Build From Source

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output binary:

- `build\Release\god_of_frames.exe`

## Run

### Single game mode

```powershell
.\build\Release\god_of_frames.exe --game helldivers2.exe --open-ui
```

### Auto-detect watcher mode

```powershell
.\build\Release\god_of_frames.exe --watch-games --open-ui
```

### Watcher override list

```powershell
.\build\Release\god_of_frames.exe --watch-games --games helldivers2.exe,forhonor.exe
```

### Protect a background app from auto-close

```powershell
.\build\Release\god_of_frames.exe --watch-games --protect-app Discord.exe
```

## CLI Reference

```text
god_of_frames --game <Game.exe> [--interval 1] [--fps-threshold 55] [--model gemini-1.5-flash] [--open-ui] [--no-elevate]
god_of_frames --watch-games [--games a.exe,b.exe] [--open-ui]
god_of_frames --install-startup | --remove-startup
```

Arguments:

- `--game <exe>`: attach to one process name.
- `--watch-games`: monitor configured game list and auto-attach.
- `--games <list>`: comma/semicolon separated watch list override.
- `--interval <sec>`: telemetry loop interval (default from settings).
- `--fps-threshold <fps>`: low FPS threshold used in severity logic.
- `--model <name>`: Gemini model name.
- `--open-ui`: open local UI on startup.
- `--no-elevate`: skip admin relaunch (can reduce FPS capture reliability).
- `--settings <path>`: alternate settings file path.
- `--protect-app <exe>`: add app to never-close list.
- `--install-startup`: install Windows login startup entry.
- `--remove-startup`: remove startup entry.

## Overlay Controls

- `F10` cycles: `full -> mini -> hidden`
- Severe FPS drop reason appears in overlay for 5 seconds.

## Local Control Website

URL:

- `http://127.0.0.1:5055`

Features:

- live telemetry cards
- settings editor (applies immediately)
- feedback submission tab
- feedback history view (local log)

API endpoints:

- `GET /api/state`
- `GET /api/health`
- `GET /api/stats`
- `POST /api/settings`
- `GET /api/feedback`
- `POST /api/feedback`

## Settings File

Default file:

- `data/settings.conf`

Example file:

- `settings.example.conf`

Fields:

- `interval_seconds`
- `fps_threshold`
- `auto_elevate`
- `open_ui_on_start`
- `feedback_endpoint`
- `update_manifest_url`
- `game_processes` (semicolon list)
- `trim_targets` (semicolon list)
- `protected_apps` (semicolon list)

## Data and Learning Artifacts

Runtime data is written under `data/`:

- `<game>.history.csv`: telemetry timeline
- `<game>.events.log`: action/notification outcomes
- `<game>.learning.csv`: learned action scores
- `dashboard.html`: local dashboard
- `settings.conf`: active settings
- `feedback.log`: local feedback submissions

## Gemini Integration (Optional)

Set your key in the shell before launch:

```powershell
$env:GEMINI_API_KEY="YOUR_KEY"
```

If unset, God of Frames runs in local-analysis mode and still learns from results.

## Packaging for End Users

```powershell
.\scripts\package_release.ps1
```

Package output folder:

- `dist\GodOfFrames\`

The release script now fails immediately if `cmake` configure/build fails. Use a fresh build directory or remove a stale one if you switch generators/platforms.

Includes:

- `god_of_frames.exe`
- `start_god_of_frames.bat`
- `run_watch_games.bat`
- `install_startup.bat`
- `remove_startup.bat`
- `run_feedback_hub.bat`
- `run_update_hub.bat`
- `settings.conf`
- `README_USER.txt`
- `feedback_hub.py`
- `update_hub.py`
- `update_manifest.json`

Notes:

- `run_feedback_hub.bat` and `run_update_hub.bat` auto-detect Python 3.
- If Python is missing or broken, those launchers stop with a clear warning instead of failing silently.

## Feedback Relay (Owner Side)

Use this when you want feedback from all users in one place.

1. Run hub on your machine/server:

```powershell
python scripts/feedback_hub.py
```

2. Set client `feedback_endpoint`:

```text
http://YOUR_SERVER:8787/api/ingest
```

3. Open owner dashboard:

```text
http://YOUR_SERVER:8787/
```

Notes:

- `run_feedback_hub.bat` includes Python auto-detection fallbacks.
- Relay requires Python 3 on the host.
- Optional relay security:
  - set `FEEDBACK_HUB_INGEST_TOKEN` on server
  - clients can use endpoint `http://YOUR_SERVER:8787/api/ingest?token=YOUR_TOKEN`
  - set `FEEDBACK_HUB_VIEW_TOKEN` to protect dashboard/API viewing

## Optional Update Manifest Server

Use this if you want a central version endpoint for future client update checks.

1. Run update hub:

```powershell
python scripts/update_hub.py
```

2. Edit manifest file:

```text
data/update_manifest.json
```

3. Point clients to:

```text
http://YOUR_SERVER:8790/api/version
```

You can persist this in app settings as `update_manifest_url`.
Optional security:

- `UPDATE_HUB_VIEW_TOKEN` to protect GET endpoints
- `UPDATE_HUB_ADMIN_TOKEN` to protect `POST /api/version`

## Troubleshooting

### FPS shows `N/A`

- Run elevated (Administrator) for reliable PresentMon session access.
- Ensure PresentMon is installed and callable.
- Confirm game process name exactly matches executable name.

### Web UI does not open

- Check `http://127.0.0.1:5055` manually.
- Ensure no local process already uses port `5055`.

### Startup install/remove issues

- Run command from normal user context (uses `HKCU\...\Run`).
- Verify entry name `GodOfFramesAutoStart` in registry.

### Feedback relay not receiving submissions

- Confirm `feedback_endpoint` URL is reachable from client machine.
- Confirm hub is running and port `8787` is open.
- Verify client post result returns `"forwarded": true`.
- If ingest token is enabled, include `?token=...` in `feedback_endpoint`.

## Safety Notes

- Automatic remediation is intentionally allow-listed.
- No direct overclocking or BIOS-level changes are performed.
- Background app trimming respects `protected_apps`.

## Public Repo Notes

If you publish this repository publicly, do not commit:

- personal API keys
- generated telemetry logs under `data/`
- local build artifacts

A `.gitignore` is provided to keep generated artifacts out of source control.

## License

No license file is included yet. Add a license before broad public distribution.
