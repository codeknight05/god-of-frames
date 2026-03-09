param(
    [string]$BuildDir = "build_dist",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "[1/5] Configuring..."
cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64

Write-Host "[2/5] Building..."
cmake --build $BuildDir --config $Config

$dist = Join-Path (Get-Location) "dist\GodOfFrames"
if (Test-Path $dist) {
    Remove-Item -Recurse -Force $dist
}
New-Item -ItemType Directory -Path $dist | Out-Null

Write-Host "[3/5] Copying user artifacts..."
Copy-Item "$BuildDir\$Config\god_of_frames.exe" "$dist\god_of_frames.exe"
if (Test-Path "settings.example.conf") {
    Copy-Item "settings.example.conf" "$dist\settings.conf"
}
if (Test-Path "scripts\update_manifest.example.json") {
    Copy-Item "scripts\update_manifest.example.json" "$dist\update_manifest.json"
}

@"
@echo off
cd /d %~dp0
start "" god_of_frames.exe --watch-games --open-ui
"@ | Set-Content -Encoding ASCII "$dist\Start God of Frames.bat"

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
God of Frames - User Guide
==========================

### Step 1: Extract the Files

Unzip this package to a permanent folder on your computer, like C:\Program Files\GodOfFrames or on your Desktop.

### Step 2: Start the Application

Double-click the "Start God of Frames.bat" file to launch the app.

- You will be asked for Administrator permission. Please click "Yes". This is required for the app to accurately monitor your game's FPS.
- Your web browser will automatically open the control panel at http://127.0.0.1:5055.

### Step 3: Add Your Games

In the web control panel, go to the "Settings" tab.

Find the "Game Watch List" text box and enter the executable names of your games, separated by a semicolon. For example: `helldivers2.exe;cyberpunk2077.exe;eldenring.exe`

### Step 4: Play Your Game!

That's it! The frame optimization is now active.

Just launch one of the games you added to the watch list. God of Frames will automatically detect it and work in the background to keep performance smooth. You can press F10 in-game to see the overlay.

### Optional: Start with Windows

If you want God of Frames to run automatically when you log in, just run the `install_startup.bat` file. You can undo this at any time by running `remove_startup.bat`.
"@ | Set-Content -Encoding ASCII "$dist\README.txt"

Write-Host "[4/5] Creating release archive..."
$manifestPath = "$dist\update_manifest.json"
$version = "1.0.0" # Default version
if (Test-Path $manifestPath) {
    $manifest = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
    if ($manifest.latest_version) {
        $version = $manifest.latest_version
    }
}
$zipFile = "dist\GodOfFrames-v$($version)-win64.zip"
Compress-Archive -Path "$dist\*" -DestinationPath $zipFile -Force

Write-Host "[5/5] Done. Package archive created at $zipFile"
