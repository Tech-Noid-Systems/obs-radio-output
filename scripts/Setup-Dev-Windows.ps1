# Setup-Dev-Windows.ps1 — Set up the obs-radio-output local development environment on Windows.
#
# What this script does:
#   1. Checks for Visual Studio 2022 (Build Tools or IDE)
#   2. Checks for / installs MSYS2
#   3. Installs libshout and pkg-config via MSYS2 MinGW64
#   4. Runs CMake configure using the 'windows-x64' preset
#
# Prerequisites:
#   - Windows 10/11 x64
#   - Run this script from the repo root in a PowerShell terminal
#   - winget must be available (ships with Windows 11 and recent Windows 10 updates)
#
# After this script completes, build the plugin with:
#   cmake --build build_x64 --config RelWithDebInfo
#
# Install the plugin to OBS with:
#   cmake --install build_x64 --config RelWithDebInfo `
#         --prefix "$env:APPDATA\obs-studio\plugins\obs-radio-output"
#
# NOTE: Windows streaming support (libshout linking against MSVC) is not yet
# fully implemented. The plugin will build and load in OBS but streaming will
# not function until MSVC-compatible libshout binaries are available.

#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot   = Split-Path -Parent $ScriptDir

function Write-Step  { param($msg) Write-Host "▶ $msg" -ForegroundColor Cyan }
function Write-Ok    { param($msg) Write-Host "✔ $msg" -ForegroundColor Green }
function Write-Warn  { param($msg) Write-Host "⚠ $msg" -ForegroundColor Yellow }
function Write-Fail  { param($msg) Write-Host "✖ $msg" -ForegroundColor Red; exit 1 }

# ── 1. Check Visual Studio 2022 ───────────────────────────────────────────────
Write-Step "Checking for Visual Studio 2022..."

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
  Write-Warn "vswhere.exe not found. Visual Studio 2022 may not be installed."
  Write-Host ""
  Write-Host "Install Visual Studio 2022 (Community or Build Tools) with the" -ForegroundColor Yellow
  Write-Host "'Desktop development with C++' workload from:" -ForegroundColor Yellow
  Write-Host "  https://visualstudio.microsoft.com/downloads/" -ForegroundColor Yellow
  Write-Host ""
  Write-Fail "Visual Studio 2022 is required. Install it and re-run this script."
}

$vsInstall = & $vsWhere -latest -version "[17,18)" -property installationPath 2>$null
if (-not $vsInstall) {
  Write-Fail "Visual Studio 2022 not found. Install it with the 'Desktop development with C++' workload."
}
Write-Ok "Visual Studio 2022 found: $vsInstall"

# ── 2. Check / install CMake ──────────────────────────────────────────────────
Write-Step "Checking for CMake..."
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  Write-Warn "CMake not found. Attempting to install via winget..."
  if (Get-Command winget -ErrorAction SilentlyContinue) {
    winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
    # Refresh PATH
    $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH", "Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("PATH", "User")
  } else {
    Write-Fail "winget not available. Install CMake manually from https://cmake.org/download/"
  }
}
$cmakeVersion = (cmake --version | Select-Object -First 1)
Write-Ok "CMake found: $cmakeVersion"

# ── 3. Check / install MSYS2 ─────────────────────────────────────────────────
Write-Step "Checking for MSYS2..."
$msys2Root = "C:\msys64"
if (-not (Test-Path "$msys2Root\usr\bin\bash.exe")) {
  Write-Warn "MSYS2 not found at $msys2Root. Attempting to install via winget..."
  if (Get-Command winget -ErrorAction SilentlyContinue) {
    winget install --id MSYS2.MSYS2 --silent --accept-package-agreements --accept-source-agreements
    if (-not (Test-Path "$msys2Root\usr\bin\bash.exe")) {
      Write-Fail "MSYS2 install did not produce expected files. Install manually from https://www.msys2.org/"
    }
  } else {
    Write-Fail "winget not available. Install MSYS2 manually from https://www.msys2.org/ to $msys2Root"
  }
}
Write-Ok "MSYS2 found at $msys2Root"

# ── 4. Install libshout via MSYS2 MinGW64 ────────────────────────────────────
Write-Step "Installing libshout and pkg-config via MSYS2 MinGW64..."
$bash = "$msys2Root\usr\bin\bash.exe"
& $bash -lc "pacman -S --noconfirm --needed mingw-w64-x86_64-libshout mingw-w64-x86_64-pkg-config" 2>&1
Write-Ok "MSYS2 MinGW64 libshout installed"

# ── 5. CMake configure ────────────────────────────────────────────────────────
Write-Step "Running CMake configure (preset: windows-x64)..."
Set-Location $RepoRoot
cmake --preset windows-x64 `
  -DLibShout_ROOT="C:/msys64/mingw64" `
  -DCMAKE_PREFIX_PATH="C:/msys64/mingw64"
Write-Ok "CMake configure complete — build directory: build_x64\"

# ── Done ──────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Dev environment ready!" -ForegroundColor Green
Write-Host ""
Write-Host "Build the plugin:" -ForegroundColor White
Write-Host "  cmake --build build_x64 --config RelWithDebInfo" -ForegroundColor Cyan
Write-Host ""
Write-Host "Install to OBS:" -ForegroundColor White
Write-Host "  cmake --install build_x64 --config RelWithDebInfo ``" -ForegroundColor Cyan
Write-Host "        --prefix `"$env:APPDATA\obs-studio\plugins\obs-radio-output`"" -ForegroundColor Cyan
Write-Host ""
Write-Host "Then launch OBS and check Help → Log Files → Current Log for:" -ForegroundColor White
Write-Host "  [obs-radio-output] plugin loaded successfully" -ForegroundColor Yellow
Write-Host ""
Write-Host "NOTE: Streaming to Icecast/SHOUTcast is not yet functional on Windows." -ForegroundColor Yellow
Write-Host "The plugin will load in OBS but the streaming output requires MSVC-compatible" -ForegroundColor Yellow
Write-Host "libshout binaries (tracked as a known issue)." -ForegroundColor Yellow
