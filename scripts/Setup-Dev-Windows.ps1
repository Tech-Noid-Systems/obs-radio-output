# Setup-Dev-Windows.ps1 — Set up the obs-radio-output local development environment on Windows.
#
# What this script does:
#   1. Checks for Visual Studio 2022 with the C++ and clang-cl (ClangCL) components
#   2. Checks for / installs CMake
#   3. Resolves a vcpkg root (uses the copy bundled with Visual Studio if needed)
#   4. Builds libshout from source as an MSVC static lib + bundles its codec deps
#      (deps/libshout-win/build.ps1 — the same step CI runs)
#   5. Runs CMake configure pointed at that prefix
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
# NOTE: Windows now builds the full streaming stack — libshout (with TLS) plus the
# MP3 / Opus / Vorbis encoders. End-to-end streaming has not yet been verified on a
# Windows test machine; see https://github.com/tech-noid-systems/obs-radio-output/issues/37.

#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

function Write-Step { param($msg) Write-Host "▶ $msg" -ForegroundColor Cyan }
function Write-Ok { param($msg) Write-Host "✔ $msg" -ForegroundColor Green }
function Write-Warn { param($msg) Write-Host "⚠ $msg" -ForegroundColor Yellow }
function Write-Fail { param($msg) Write-Host "✖ $msg" -ForegroundColor Red; exit 1 }

# ── 1. Check Visual Studio 2022 (+ clang-cl) ─────────────────────────────────
Write-Step "Checking for Visual Studio 2022..."

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
  Write-Warn "vswhere.exe not found. Visual Studio 2022 may not be installed."
  Write-Host ""
  Write-Host "Install Visual Studio 2022 (Community or Build Tools) with the" -ForegroundColor Yellow
  Write-Host "'Desktop development with C++' workload AND the 'C++ Clang tools for Windows'" -ForegroundColor Yellow
  Write-Host "component, from: https://visualstudio.microsoft.com/downloads/" -ForegroundColor Yellow
  Write-Host ""
  Write-Fail "Visual Studio 2022 is required. Install it and re-run this script."
}

$vsInstall = & $vsWhere -latest -version "[17,18)" -property installationPath 2>$null
if (-not $vsInstall) {
  Write-Fail "Visual Studio 2022 not found. Install it with the 'Desktop development with C++' workload."
}
Write-Ok "Visual Studio 2022 found: $vsInstall"

# libshout is built with clang-cl (it uses the GCC void-pointer-arithmetic
# extension that the MSVC cl compiler rejects).  Warn — don't fail — if the
# ClangCL toolset component isn't detected; detection isn't always reliable.
$clangVs = & $vsWhere -latest -version "[17,18)" `
  -requires Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset `
  -property installationPath 2>$null
if (-not $clangVs) {
  Write-Warn "The 'C++ Clang tools for Windows' (ClangCL) component was not detected."
  Write-Warn "libshout needs clang-cl. In the VS Installer, Modify -> Individual components ->"
  Write-Warn "add 'C++ Clang Compiler for Windows' and 'MSBuild support for LLVM (clang-cl) toolset'."
} else {
  Write-Ok "ClangCL toolset present."
}

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

# ── 3. Resolve a vcpkg root ──────────────────────────────────────────────────
# The libshout build pulls ogg/vorbis/openssl/pthreads/mp3lame/opus from vcpkg.
# Prefer an existing VCPKG_INSTALLATION_ROOT; otherwise fall back to the copy
# bundled with Visual Studio 2022 (VC\vcpkg).
Write-Step "Resolving vcpkg..."
if (-not $env:VCPKG_INSTALLATION_ROOT) {
  $vsVcpkg = Join-Path $vsInstall 'VC\vcpkg'
  if (Test-Path (Join-Path $vsVcpkg 'scripts\buildsystems\vcpkg.cmake')) {
    $env:VCPKG_INSTALLATION_ROOT = $vsVcpkg
  } else {
    Write-Fail "vcpkg not found. Set VCPKG_INSTALLATION_ROOT, or install vcpkg (https://vcpkg.io)."
  }
}
Write-Ok "Using vcpkg at $($env:VCPKG_INSTALLATION_ROOT)"

# ── 4. Build libshout (+ bundled codec deps) from source ─────────────────────
Write-Step "Building libshout from source (clang-cl + vcpkg)... this takes a few minutes."
$LibShoutPrefix = Join-Path $RepoRoot 'build-deps\libshout'
& "$RepoRoot\deps\libshout-win\build.ps1" -InstallPrefix $LibShoutPrefix
Write-Ok "libshout built and installed to $LibShoutPrefix"

# ── 5. CMake configure ────────────────────────────────────────────────────────
Write-Step "Running CMake configure (preset: windows-x64)..."
Set-Location $RepoRoot
cmake --preset windows-x64 `
  -DLibShout_ROOT="$LibShoutPrefix" `
  -DLibLame_ROOT="$LibShoutPrefix" `
  -DLibOpus_ROOT="$LibShoutPrefix" `
  -DLibOgg_ROOT="$LibShoutPrefix" `
  -DLibVorbis_ROOT="$LibShoutPrefix"
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
Write-Host "Then launch OBS and check Help -> Log Files -> Current Log for:" -ForegroundColor White
Write-Host "  [obs-radio-output] plugin loaded successfully" -ForegroundColor Yellow
Write-Host ""
Write-Host "NOTE: Windows builds the full streaming stack (libshout + MP3/Opus/Vorbis + TLS)," -ForegroundColor Yellow
Write-Host "but end-to-end streaming has not yet been verified on Windows — see issue #37." -ForegroundColor Yellow
