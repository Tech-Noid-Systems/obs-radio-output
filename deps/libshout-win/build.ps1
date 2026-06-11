<#
.SYNOPSIS
  Download, patch, and build stock Xiph libshout 2.4.6 as an MSVC static library,
  installing it into a prefix the plugin's FindLibShout.cmake can consume.

.DESCRIPTION
  Implements issue #37's "vendor stock 2.4.6 + cherry-picked Windows patches"
  path.  Mirrors how .github/scripts/build-macos builds libshout from the release
  tarball at CI time.  Requires a vcpkg toolchain providing ogg/vorbis/openssl/
  pthreads for the x64-windows-static-md triplet (static libs, dynamic CRT — so
  the resulting plugin matches OBS's /MD runtime with no extra DLLs to ship).

.PARAMETER InstallPrefix
  Where to install shout.lib + include/shout/shout.h (default: <repo>/build-deps/libshout).

.PARAMETER VcpkgRoot
  vcpkg installation root (default: $env:VCPKG_INSTALLATION_ROOT, set on GitHub runners).
#>
[CmdletBinding()]
param(
    [string] $InstallPrefix,
    [string] $VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
)

$ErrorActionPreference = 'Stop'

$Here = $PSScriptRoot
$RepoRoot = (Resolve-Path "$Here/../..").Path
if ( -not $InstallPrefix ) { $InstallPrefix = Join-Path $RepoRoot 'build-deps/libshout' }
if ( -not $VcpkgRoot ) { throw 'vcpkg root not found: pass -VcpkgRoot or set VCPKG_INSTALLATION_ROOT' }

$Version = '2.4.6'
$Sha256 = '39cbd4f0efdfddc9755d88217e47f8f2d7108fa767f9d58a2ba26a16d8f7c910'
$Work = Join-Path $RepoRoot 'build-deps/_libshout-src'
$Tarball = Join-Path $RepoRoot "build-deps/libshout-$Version.tar.gz"
$Src = Join-Path $Work "libshout-$Version"

New-Item -ItemType Directory -Force -Path (Split-Path $Tarball) | Out-Null
Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Work | Out-Null

Write-Host "==> Downloading libshout $Version"
curl.exe -sSL --retry 3 "https://downloads.xiph.org/releases/libshout/libshout-$Version.tar.gz" -o $Tarball
$actual = (Get-FileHash -Algorithm SHA256 $Tarball).Hash.ToLower()
if ( $actual -ne $Sha256 ) { throw "libshout tarball SHA256 mismatch: expected $Sha256, got $actual" }

Write-Host "==> Extracting + patching"
tar -xzf $Tarball -C $Work
# Apply windows-msvc.patch with GNU patch (ships with Git for Windows). -p1
# strips the a//b/ prefix. Resolve patch.exe explicitly — Git's usr/bin is not
# always on the runner PATH.
$patchExe = (Get-Command patch -ErrorAction SilentlyContinue).Source
if ( -not $patchExe ) {
    $gitDir = Split-Path (Split-Path (Get-Command git).Source)
    $patchExe = Join-Path $gitDir 'usr/bin/patch.exe'
}
if ( -not (Test-Path $patchExe) ) { throw "GNU patch not found (looked for $patchExe)" }
Push-Location $Src
& $patchExe -p1 -i "$Here/windows-msvc.patch"
if ( $LASTEXITCODE -ne 0 ) { throw 'failed to apply windows-msvc.patch' }
Pop-Location

$Toolchain = (Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake') -replace '\\', '/'
$SrcFwd = $Src -replace '\\', '/'
$PrefixFwd = $InstallPrefix -replace '\\', '/'
$BuildDir = Join-Path $RepoRoot 'build-deps/_libshout-build'

Write-Host "==> Configuring (MSVC + vcpkg static-md deps)"
cmake -S "$Here" -B "$BuildDir" -A x64 `
    "-DLIBSHOUT_SRC=$SrcFwd" `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md" `
    "-DCMAKE_INSTALL_PREFIX=$PrefixFwd"
if ( $LASTEXITCODE -ne 0 ) { throw 'libshout configure failed' }

Write-Host "==> Building + installing"
cmake --build "$BuildDir" --config Release --verbose
if ( $LASTEXITCODE -ne 0 ) { throw 'libshout build failed' }
cmake --install "$BuildDir" --config Release
if ( $LASTEXITCODE -ne 0 ) { throw 'libshout install failed' }

$Lib = Join-Path $InstallPrefix 'lib/shout.lib'
if ( -not (Test-Path $Lib) ) { throw "shout.lib not produced at $Lib" }
$kb = [math]::Round((Get-Item $Lib).Length / 1KB)
Write-Host "==> shout.lib installed: $Lib ($kb KB)"

# TLS sanity check: shout_tls_new only exists when HAVE_OPENSSL compiled in
# (tls.c wraps its body in #ifdef HAVE_OPENSSL).  Analogous to the macOS
# build's nm grep.  dumpbin ships with the MSVC toolchain on PATH at this point.
$symbols = & dumpbin /SYMBOLS $Lib 2>$null | Select-String 'shout_tls_new'
if ( -not $symbols ) {
    throw 'shout.lib has no shout_tls_new symbol — TLS (HAVE_OPENSSL) did not compile in'
}
Write-Host "==> TLS symbol present (shout_tls_new) — libshout built with OpenSSL"
