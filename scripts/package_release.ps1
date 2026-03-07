param(
    [string]$BuildDir = "build_dist",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "[1/4] Configuring..."
cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64

Write-Host "[2/4] Building..."
cmake --build $BuildDir --config $Config

$dist = Join-Path (Get-Location) "dist\GodOfFrames"
if (Test-Path $dist) {
    Remove-Item -Recurse -Force $dist
}
New-Item -ItemType Directory -Path $dist | Out-Null

Write-Host "[3/4] Copying artifacts..."
Copy-Item "$BuildDir\$Config\god_of_frames.exe" "$dist\god_of_frames.exe"
Copy-Item "README.md" "$dist\README.md"
if (Test-Path "settings.example.conf") {
    Copy-Item "settings.example.conf" "$dist\settings.conf"
}
if (Test-Path "scripts\feedback_hub.py") {
    Copy-Item "scripts\feedback_hub.py" "$dist\feedback_hub.py"
}

@"
@echo off
cd /d %~dp0
god_of_frames.exe --watch-games --open-ui
"@ | Set-Content -Encoding ASCII "$dist\run_watch_games.bat"

@"
@echo off
cd /d %~dp0
start "" god_of_frames.exe --watch-games --open-ui
"@ | Set-Content -Encoding ASCII "$dist\start_god_of_frames.bat"

@"
@echo off
cd /d %~dp0
god_of_frames.exe --install-startup
"@ | Set-Content -Encoding ASCII "$dist\install_startup.bat"

@"
@echo off
cd /d %~dp0
god_of_frames.exe --remove-startup
"@ | Set-Content -Encoding ASCII "$dist\remove_startup.bat"

@"
@echo off
setlocal
cd /d %~dp0

set "PY_CMD="

python --version >nul 2>nul
if "%ERRORLEVEL%"=="0" set "PY_CMD=python"

if not defined PY_CMD (
  py -3 --version >nul 2>nul
  if "%ERRORLEVEL%"=="0" set "PY_CMD=py -3"
)

if not defined PY_CMD if exist "%~dp0python.exe" set "PY_CMD=%~dp0python.exe"
if not defined PY_CMD if exist "%~dp0python\python.exe" set "PY_CMD=%~dp0python\python.exe"

if not defined PY_CMD (
  for /f "delims=" %%P in ('dir /b /s "%LOCALAPPDATA%\Programs\Python\python.exe" 2^>nul') do (
    set "PY_CMD=%%P"
    goto :run_hub
  )
)

if not defined PY_CMD (
  for /f "delims=" %%P in ('dir /b /s "%LOCALAPPDATA%\Temp\WinGet\python.exe" 2^>nul') do (
    set "PY_CMD=%%P"
    goto :run_hub
  )
)

:run_hub
if not defined PY_CMD (
  echo Could not find a working Python 3 runtime.
  echo Install Python 3 and run this script again.
  pause
  exit /b 1
)

echo Using Python: %PY_CMD%
%PY_CMD% feedback_hub.py
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo Feedback hub exited with code %RC%.
  pause
)
exit /b %RC%
"@ | Set-Content -Encoding ASCII "$dist\run_feedback_hub.bat"

@"
God of Frames - Quick Start (No VS Code needed)

1) Double-click start_god_of_frames.bat
2) Allow Administrator prompt (recommended for accurate FPS)
3) Website opens automatically at http://127.0.0.1:5055
4) In website:
   - Settings tab: choose protected apps and game watch list
   - Feedback tab: send bug/feature reports
5) Optional auto-start at login:
   - run install_startup.bat

For owner-only cross-user feedback collection:
- Host feedback_hub.py on a server (python feedback_hub.py or run_feedback_hub.bat)
- Set feedback_endpoint in settings.conf to:
  http://YOUR_SERVER:8787/api/ingest
"@ | Set-Content -Encoding ASCII "$dist\README_USER.txt"

Write-Host "[4/4] Done. Package folder: $dist"

