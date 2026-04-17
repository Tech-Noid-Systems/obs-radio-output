#!/usr/bin/env bash
# run-clang-tidy.sh — Run clang-tidy locally using the project's .clang-tidy config.
#
# NOTE: macOS is not supported — OBS requires the Xcode CMake generator, which
# does not produce compile_commands.json and macOS SIP prevents bear from
# intercepting Xcode compiler calls.  Use Docker or rely on the CI check.
#
# Docker usage (from repo root):
#   docker run --rm -v "$(pwd):/src" ubuntu:24.04 bash /src/scripts/run-clang-tidy.sh
#
# Linux usage:
#   ./scripts/run-clang-tidy.sh              # check all src/ files
#   ./scripts/run-clang-tidy.sh src/radio-output.c   # check one file
#
# Requirements (Linux): apt-get install clang-tidy cmake libshout3-dev
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-tidy"
COMPILE_DB="${BUILD_DIR}/compile_commands.json"

# ---------------------------------------------------------------------------
# Locate clang-tidy — prefer Homebrew LLVM over Apple's (missing many checks)
# ---------------------------------------------------------------------------
CLANG_TIDY=""
for candidate in \
    "/opt/homebrew/opt/llvm/bin/clang-tidy" \
    "/usr/local/opt/llvm/bin/clang-tidy" \
    "$(command -v clang-tidy 2>/dev/null || true)"; do
    if [[ -x "${candidate}" ]]; then
        CLANG_TIDY="${candidate}"
        break
    fi
done

if [[ -z "${CLANG_TIDY}" ]]; then
    echo "error: clang-tidy not found. Install it with: brew install llvm" >&2
    exit 1
fi

echo "Using: ${CLANG_TIDY}"
echo "Version: $("${CLANG_TIDY}" --version | head -1)"
echo ""

# ---------------------------------------------------------------------------
# Generate compile_commands.json
#
# macOS: OBS requires the Xcode generator, which does not support
#        CMAKE_EXPORT_COMPILE_COMMANDS.  macOS SIP also blocks bear from
#        intercepting Xcode's compiler calls.  Run via Docker instead:
#
#   docker run --rm -v "$(pwd):/src" ubuntu:24.04 bash /src/scripts/run-clang-tidy.sh
#
# Linux: standard cmake flag works fine with the Makefile/Ninja generator.
# ---------------------------------------------------------------------------
if [[ "$(uname)" == "Darwin" ]]; then
    echo "macOS is not supported for local clang-tidy runs." >&2
    echo "" >&2
    echo "OBS requires the Xcode CMake generator, which does not produce" >&2
    echo "compile_commands.json. macOS SIP also prevents bear from intercepting" >&2
    echo "Xcode's compiler calls." >&2
    echo "" >&2
    echo "Use Docker to run this script on Linux instead:" >&2
    echo "  docker run --rm -v \"\$(pwd):/src\" ubuntu:24.04 bash /src/scripts/run-clang-tidy.sh" >&2
    echo "" >&2
    echo "Or rely on the clang-tidy CI check which runs on every PR." >&2
    exit 1
fi

if [[ ! -f "${COMPILE_DB}" ]]; then
    echo "compile_commands.json not found — running cmake configure..."
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
    # Refresh the existing cache silently
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null 2>&1
fi

echo ""

# ---------------------------------------------------------------------------
# Run clang-tidy
# ---------------------------------------------------------------------------
if [[ $# -gt 0 ]]; then
    FILES=("$@")
else
    mapfile -t FILES < <(find "${REPO_ROOT}/src" -name "*.c" -o -name "*.h")
fi

echo "Checking ${#FILES[@]} file(s)..."
echo ""

"${CLANG_TIDY}" \
    -p "${BUILD_DIR}" \
    --config-file="${REPO_ROOT}/.clang-tidy" \
    "${FILES[@]}"
