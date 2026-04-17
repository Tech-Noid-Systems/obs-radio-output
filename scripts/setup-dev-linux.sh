#!/usr/bin/env bash
# setup-dev-linux.sh — Set up the obs-radio-output local development environment on Linux.
#
# Supports Debian/Ubuntu-based distributions.
#
# What this script does:
#   1. Checks prerequisites (sudo access, apt)
#   2. Installs required apt packages
#   3. Runs CMake configure using the 'linux-x86_64' or 'linux-aarch64' preset
#
# After this script completes, build the plugin with:
#   cmake --build build_linux_x86_64 --config RelWithDebInfo
#
# Install the plugin to OBS with:
#   cmake --install build_linux_x86_64 --config RelWithDebInfo \
#         --prefix ~/.config/obs-studio/plugins/obs-radio-output
#
# Then launch OBS and check Help → Log Files → Current Log for:
#   [obs-radio-output] plugin loaded successfully

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "${SCRIPT_DIR}")"

# ── Colour helpers ────────────────────────────────────────────────────────────
print_step()  { echo -e "\033[36m▶ $1\033[0m"; }
print_ok()    { echo -e "\033[32m✔ $1\033[0m"; }
print_warn()  { echo -e "\033[33m⚠ $1\033[0m"; }
print_error() { echo -e "\033[31m✖ $1\033[0m" >&2; }

# ── 1. Prerequisites ──────────────────────────────────────────────────────────
print_step "Checking prerequisites..."

if ! command -v apt-get &>/dev/null; then
  print_error "apt-get not found. This script requires a Debian/Ubuntu-based distribution."
  exit 1
fi

if ! sudo -n true 2>/dev/null; then
  print_warn "This script requires sudo access to install packages."
fi
print_ok "Prerequisites OK"

# ── 2. Detect architecture ────────────────────────────────────────────────────
ARCH="$(uname -m)"
case "${ARCH}" in
  x86_64)  CMAKE_PRESET="linux-x86_64" ;;
  aarch64) CMAKE_PRESET="linux-aarch64" ;;
  *)
    print_error "Unsupported architecture: ${ARCH}"
    exit 1
    ;;
esac
print_ok "Architecture: ${ARCH} — using CMake preset: ${CMAKE_PRESET}"

# ── 3. Install apt packages ───────────────────────────────────────────────────
print_step "Installing apt packages..."
sudo apt-get update -qq
sudo apt-get install -y \
  build-essential \
  ccache \
  cmake \
  git \
  jq \
  libshout3-dev \
  ninja-build \
  pkg-config
print_ok "Packages installed"

# ── 4. CMake configure ────────────────────────────────────────────────────────
print_step "Running CMake configure (preset: ${CMAKE_PRESET})..."
cd "${REPO_ROOT}"
cmake --preset "${CMAKE_PRESET}"
print_ok "CMake configure complete — build directory: build_${CMAKE_PRESET/linux-/linux_}/"

BUILD_DIR="build_${CMAKE_PRESET/linux-/linux_}"

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo -e "\033[32mDev environment ready!\033[0m"
echo ""
echo "Build the plugin:"
echo -e "  \033[36mcmake --build ${BUILD_DIR} --config RelWithDebInfo\033[0m"
echo ""
echo "Install to OBS:"
echo -e "  \033[36mcmake --install ${BUILD_DIR} --config RelWithDebInfo \\\033[0m"
echo -e "  \033[36m      --prefix ~/.config/obs-studio/plugins/obs-radio-output\033[0m"
echo ""
echo "Then launch OBS and check Help → Log Files → Current Log for:"
echo -e "  \033[33m[obs-radio-output] plugin loaded successfully\033[0m"
