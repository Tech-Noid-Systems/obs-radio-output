#!/usr/bin/env zsh
# setup-dev-macos.sh — Set up the obs-radio-output local development environment on macOS.
#
# What this script does:
#   1. Checks prerequisites (Xcode Command Line Tools, Homebrew)
#   2. Installs required Homebrew packages from .github/scripts/.Brewfile
#   3. Builds a Universal Binary (arm64 + x86_64) libogg and libshout from source
#   4. Runs CMake configure using the 'macos' preset
#
# After this script completes, build the plugin with:
#   xcodebuild -project build_macos/obs-radio-output.xcodeproj \
#              -target obs-radio-output \
#              -configuration RelWithDebInfo \
#              ONLY_ACTIVE_ARCH=NO \
#              -arch arm64 -arch x86_64
#
# Install the plugin to OBS with:
#   cmake --install build_macos --config RelWithDebInfo --prefix release/RelWithDebInfo
#   # Then copy release/ contents to:
#   # ~/Library/Application Support/obs-studio/plugins/obs-radio-output/

builtin emulate -L zsh
setopt ERR_EXIT ERR_RETURN PIPE_FAIL NO_UNSET

SCRIPT_DIR=${0:A:h}
REPO_ROOT=${SCRIPT_DIR:h}

# ── Colour helpers ────────────────────────────────────────────────────────────
print_step()  { print -P "%F{cyan}▶ $1%f" }
print_ok()    { print -P "%F{green}✔ $1%f" }
print_warn()  { print -P "%F{yellow}⚠ $1%f" }
print_error() { print -P "%F{red}✖ $1%f" >&2 }

# ── 1. Prerequisites ──────────────────────────────────────────────────────────
print_step "Checking prerequisites..."

if ! xcode-select -p &>/dev/null; then
  print_error "Xcode Command Line Tools not found."
  print "Install them with: xcode-select --install"
  print "Then re-run this script."
  exit 1
fi
print_ok "Xcode Command Line Tools found"

if ! command -v brew &>/dev/null; then
  print_error "Homebrew not found."
  print "Install it from https://brew.sh then re-run this script."
  exit 1
fi
print_ok "Homebrew found"

# ── 2. Install Homebrew dependencies ─────────────────────────────────────────
print_step "Installing Homebrew dependencies from .Brewfile..."
brew bundle --file "${REPO_ROOT}/.github/scripts/.Brewfile"
print_ok "Homebrew dependencies installed"

# ── 3. Build Universal libogg ─────────────────────────────────────────────────
if [[ -f /tmp/libogg-universal-done ]]; then
  print_ok "Universal libogg already built (delete /tmp/libogg-universal-done to rebuild)"
else
  print_step "Building Universal libogg (libshout dependency)..."
  curl -sSL https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.gz \
    -o /tmp/libogg.tar.gz
  tar -xzf /tmp/libogg.tar.gz -C /tmp
  mv /tmp/libogg-1.3.5 /tmp/libogg-src
  pushd /tmp/libogg-src

  mkdir -p build-arm64 && pushd build-arm64
  ../configure --prefix=/tmp/libogg-arm64 \
    CFLAGS="-arch arm64" LDFLAGS="-arch arm64"
  make -j$(sysctl -n hw.ncpu) && make install
  popd

  mkdir -p build-x86_64 && pushd build-x86_64
  ../configure --prefix=/tmp/libogg-x86_64 \
    CFLAGS="-arch x86_64" LDFLAGS="-arch x86_64"
  make -j$(sysctl -n hw.ncpu) && make install
  popd

  popd
  touch /tmp/libogg-universal-done
  print_ok "Universal libogg built"
fi

# ── 4. Build Universal libshout ───────────────────────────────────────────────
if [[ -f /tmp/libshout-universal-done ]]; then
  print_ok "Universal libshout already built (delete /tmp/libshout-universal-done to rebuild)"
else
  print_step "Building Universal libshout..."
  curl -sSL https://downloads.xiph.org/releases/libshout/libshout-2.4.6.tar.gz \
    -o /tmp/libshout.tar.gz
  tar -xzf /tmp/libshout.tar.gz -C /tmp
  mv /tmp/libshout-2.4.6 /tmp/icecast-libshout
  pushd /tmp/icecast-libshout

  # arm64 slice
  mkdir -p build-arm64 && pushd build-arm64
  PKG_CONFIG_LIBDIR=/tmp/libogg-arm64/lib/pkgconfig \
    ../configure --prefix=/tmp/libshout-arm64 \
      --without-openssl --without-theora --without-speex --without-opus \
      CFLAGS="-arch arm64 -Wno-error=implicit-function-declaration" \
      LDFLAGS="-arch arm64"
  make -j$(sysctl -n hw.ncpu) && make install
  popd

  # x86_64 slice
  mkdir -p build-x86_64 && pushd build-x86_64
  PKG_CONFIG_LIBDIR=/tmp/libogg-x86_64/lib/pkgconfig \
    ../configure --prefix=/tmp/libshout-x86_64 \
      --without-openssl --without-theora --without-speex --without-opus \
      CFLAGS="-arch x86_64 -Wno-error=implicit-function-declaration" \
      LDFLAGS="-arch x86_64"
  make -j$(sysctl -n hw.ncpu) && make install
  popd

  # Merge with lipo
  mkdir -p /tmp/libshout-universal/lib /tmp/libshout-universal/include
  lipo -create \
    /tmp/libshout-arm64/lib/libshout.dylib \
    /tmp/libshout-x86_64/lib/libshout.dylib \
    -output /tmp/libshout-universal/lib/libshout.dylib
  cp -r /tmp/libshout-arm64/include/shout /tmp/libshout-universal/include/

  popd
  touch /tmp/libshout-universal-done
  print_ok "Universal libshout built"
fi

# ── 5. CMake configure ────────────────────────────────────────────────────────
print_step "Running CMake configure (preset: macos)..."
pushd "${REPO_ROOT}"
# Default to ad-hoc signing ("-") if no Developer ID is set in the environment.
# This matches what the CI build-macos script does via CODESIGN_IDENT="${CODESIGN_IDENT:--}".
export CODESIGN_IDENT="${CODESIGN_IDENT:--}"
cmake --preset macos -DLibShout_ROOT=/tmp/libshout-universal
popd
print_ok "CMake configure complete — build directory: build_macos/"

# ── Write helper script ───────────────────────────────────────────────────────
cat > "${REPO_ROOT}/build-and-install-macos.sh" <<'EOF'
#!/usr/bin/env zsh
set -euo pipefail
REPO_ROOT="${0:A:h}"
cd "${REPO_ROOT}"

echo "Building obs-radio-output..."
xcodebuild \
  -project build_macos/obs-radio-output.xcodeproj \
  -target obs-radio-output \
  -configuration RelWithDebInfo \
  ONLY_ACTIVE_ARCH=NO \
  -arch arm64 \
  -arch x86_64 \
  | tail -5

echo "Installing to OBS plugins directory..."
cmake --install build_macos --config RelWithDebInfo --prefix release/RelWithDebInfo
cp -r "release/RelWithDebInfo/obs-radio-output.plugin" \
  "${HOME}/Library/Application Support/obs-studio/plugins/"

echo ""
echo "Done. Launch OBS and check Help → Log Files → Current Log for:"
echo "  [obs-radio-output] plugin loaded successfully"
EOF
chmod +x "${REPO_ROOT}/build-and-install-macos.sh"

# ── Done ──────────────────────────────────────────────────────────────────────
print -P ""
print -P "%F{green}Dev environment ready!%f"
print -P ""
print -P "Build and install the plugin:"
print -P "  %F{cyan}zsh build-and-install-macos.sh%f"
print -P ""
print -P "Then launch OBS and check Help → Log Files → Current Log for:"
print -P "  %F{yellow}[obs-radio-output] plugin loaded successfully%f"
