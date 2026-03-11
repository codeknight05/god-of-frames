param(
    [string]$BuildDir = "build_dist",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

function Invoke-NativeStep {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Label,
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [string[]]$Arguments = @()
    )

    Write-Host $Label
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Write-AsciiFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Content
    )

    Set-Content -Path $Path -Value $Content -Encoding ASCII
}

function Write-PythonLauncher {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$ScriptName,
        [Parameter(Mandatory = $true)]
        [string]$Title
    )

    $launcher = @"
@echo off
setlocal
cd /d %~dp0
set "PYTHON_EXE="

call :try_python python
if defined PYTHON_EXE goto run

where py >nul 2>nul
if not errorlevel 1 (
  py -3 -c "import sys" >nul 2>nul
  if not errorlevel 1 (
    set "PYTHON_EXE=py -3"
    goto run
  )
)

if exist "%~dp0python.exe" (
  "%~dp0python.exe" -c "import sys" >nul 2>nul
  if not errorlevel 1 (
    set "PYTHON_EXE=%~dp0python.exe"
    goto run
  )
)

if exist "%~dp0python\python.exe" (
  "%~dp0python\python.exe" -c "import sys" >nul 2>nul
  if not errorlevel 1 (
    set "PYTHON_EXE=%~dp0python\python.exe"
    goto run
  )
)

for /f "delims=" %%I in ('dir /b /s "%LOCALAPPDATA%\Programs\Python\python.exe" 2^>nul') do (
  call :try_python "%%~fI"
  if defined PYTHON_EXE goto run
)

for /f "delims=" %%I in ('dir /b /s "%LOCALAPPDATA%\Temp\WinGet\python.exe" 2^>nul') do (
  call :try_python "%%~fI"
  if defined PYTHON_EXE goto run
)

echo [God of Frames] Python 3 was not found.
echo This helper is optional. Install Python 3 and run this file again.
echo It is only required for $Title.
pause
exit /b 1

:run
echo [God of Frames] Starting $Title using %PYTHON_EXE%
%PYTHON_EXE% "%~dp0$ScriptName"
exit /b %errorlevel%

:try_python
%~1 -c "import sys" >nul 2>nul
if errorlevel 1 goto :eof
set "PYTHON_EXE=%~1"
goto :eof
"@

    Write-AsciiFile -Path $Path -Content $launcher
}

Invoke-NativeStep -Label "[1/5] Configuring..." -FilePath "cmake" -Arguments @(
    "-S", ".",
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64"
)

Invoke-NativeStep -Label "[2/5] Building..." -FilePath "cmake" -Arguments @(
    "--build", $BuildDir,
    "--config", $Config
)

$distRoot = Join-Path (Get-Location) "dist"
$dist = Join-Path $distRoot "GodOfFrames"
if (Test-Path $dist) {
    Remove-Item -Recurse -Force $dist
}
New-Item -ItemType Directory -Path $dist | Out-Null

Write-Host "[3/5] Copying user artifacts..."
Copy-Item "$BuildDir\$Config\god_of_frames.exe" "$dist\god_of_frames.exe"
Copy-Item "README.md" "$dist\README.md"
Copy-Item "scripts\feedback_hub.py" "$dist\feedback_hub.py"
Copy-Item "scripts\update_hub.py" "$dist\update_hub.py"
if (Test-Path "settings.example.conf") {
    Copy-Item "settings.example.conf" "$dist\settings.conf"
}
if (Test-Path "scripts\update_manifest.example.json") {
    Copy-Item "scripts\update_manifest.example.json" "$dist\update_manifest.json"
}

Write-AsciiFile -Path "$dist\start_god_of_frames.bat" -Content @"
@echo off
cd /d %~dp0
start "" god_of_frames.exe --watch-games --open-ui
"@

Write-AsciiFile -Path "$dist\run_watch_games.bat" -Content @"
@echo off
cd /d %~dp0
god_of_frames.exe --watch-games --open-ui
"@

Write-AsciiFile -Path "$dist\install_startup.bat" -Content @"
@echo off
cd /d %~dp0
god_of_frames.exe --install-startup
"@

Write-AsciiFile -Path "$dist\remove_startup.bat" -Content @"
@echo off
cd /d %~dp0
god_of_frames.exe --remove-startup
"@

Write-PythonLauncher -Path "$dist\run_feedback_hub.bat" -ScriptName "feedback_hub.py" -Title "the feedback relay server"
Write-PythonLauncher -Path "$dist\run_update_hub.bat" -ScriptName "update_hub.py" -Title "the update manifest server"

Write-AsciiFile -Path "$dist\README_USER.txt" -Content @"
God of Frames - User Guide
==========================

1. Extract the zip to a permanent folder.
2. Double-click start_god_of_frames.bat.
3. Approve the Administrator prompt if Windows shows one.
4. Open http://127.0.0.1:5055 if the browser does not open automatically.
5. Add your game executable names in the Settings tab.

Optional tools:
- run_feedback_hub.bat starts the owner feedback relay server.
- run_update_hub.bat starts the owner update manifest server.
- Both optional hub scripts require Python 3 and will warn clearly if Python is missing.
"@

Write-Host "[4/5] Creating release archive..."
$manifestPath = "$dist\update_manifest.json"
$version = "1.0.0"
if (Test-Path $manifestPath) {
    $manifest = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
    if ($manifest.latest_version) {
        $version = $manifest.latest_version
    }
}
$zipFile = Join-Path $distRoot "GodOfFrames-v$($version)-win64.zip"
Compress-Archive -Path "$dist\*" -DestinationPath $zipFile -Force

Write-Host "[5/5] Done. Package archive created at $zipFile"
