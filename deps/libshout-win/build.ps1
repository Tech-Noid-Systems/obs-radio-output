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
# Apply windows-msvc.patch with `git apply` rather than Git-for-Windows'
# patch.exe — the latter is a ported GNU patch 2.5.9 that aborts with
# "Assertation failed!" on this runner. git apply is reliable and always
# present. -p1 strips the a//b/ prefix; --directory re-roots onto the extracted
# tree. Run from the repo root (valid work-tree context) with a relative
# directory so git accepts the in-tree target.
$relSrc = [System.IO.Path]::GetRelativePath($RepoRoot, $Src) -replace '\\', '/'
Push-Location $RepoRoot
git apply -p1 --directory="$relSrc" "$Here/windows-msvc.patch"
if ( $LASTEXITCODE -ne 0 ) { throw 'failed to apply windows-msvc.patch' }
Pop-Location

$Toolchain = (Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake') -replace '\\', '/'
$SrcFwd = $Src -replace '\\', '/'
$PrefixFwd = $InstallPrefix -replace '\\', '/'
$BuildDir = Join-Path $RepoRoot 'build-deps/_libshout-build'

Write-Host "==> Configuring (clang-cl + vcpkg static-md deps)"
# -T ClangCL: build libshout with clang-cl, not cl. libshout's sources use the
# GCC void-pointer-arithmetic extension (e.g. encoding.c's `void *buf` + offset),
# which cl rejects (C2036) but clang-cl accepts (a warning, not an error) while
# still emitting MSVC-ABI objects that link cleanly into the cl-built plugin.
# VCPKG_MANIFEST_DIR points at the repo root so manifest mode installs the
# ogg/vorbis/openssl/pthreads declared in the root vcpkg.json (the libshout
# build source dir has no manifest of its own).
$RepoRootFwd = $RepoRoot -replace '\\', '/'
cmake -S "$Here" -B "$BuildDir" -A x64 -T ClangCL `
    "-DLIBSHOUT_SRC=$SrcFwd" `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md" `
    "-DVCPKG_MANIFEST_DIR=$RepoRootFwd" `
    "-DVCPKG_MANIFEST_INSTALL=ON" `
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
# (tls.c wraps its body in #ifdef HAVE_OPENSSL).  Analogous to the macOS build's
# nm grep.  dumpbin isn't on the bare-pwsh PATH (it needs the VS dev env), so
# locate it via vswhere.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$dumpbin = $null
if ( Test-Path $vswhere ) {
    $dumpbin = & $vswhere -latest -products * -find 'VC/Tools/MSVC/**/bin/Hostx64/x64/dumpbin.exe' |
        Select-Object -First 1
}
if ( -not $dumpbin ) {
    $dumpbin = (Get-Command dumpbin -ErrorAction SilentlyContinue).Source
}
if ( -not $dumpbin ) { throw 'dumpbin not found (needed for the TLS symbol check)' }
$symbols = & $dumpbin /SYMBOLS $Lib 2>$null | Select-String 'shout_tls_new'
if ( -not $symbols ) {
    throw 'shout.lib has no shout_tls_new symbol — TLS (HAVE_OPENSSL) did not compile in'
}
Write-Host "==> TLS symbol present (shout_tls_new) — libshout built with OpenSSL"
