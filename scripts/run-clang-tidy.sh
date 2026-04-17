#!/usr/bin/env bash
# run-clang-tidy.sh — Run clang-tidy locally using the project's .clang-tidy config.
#
# Usage:
#   ./scripts/run-clang-tidy.sh              # check all src/ files
#   ./scripts/run-clang-tidy.sh src/radio-output.c   # check one file
#
# Requirements: brew install llvm
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-tidy"

# Prefer Homebrew LLVM clang-tidy over Apple's (which lacks many checks)
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

# macOS requires the Xcode generator; Linux uses the default (Ninja/Make)
CMAKE_EXTRA_ARGS=()
if [[ "$(uname)" == "Darwin" ]]; then
    CMAKE_EXTRA_ARGS+=(-G Xcode)
fi

# Generate/refresh compile_commands.json if needed
if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo ""
    echo "compile_commands.json not found — running cmake configure..."
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        "${CMAKE_EXTRA_ARGS[@]}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
    # Refresh the flag in the existing cache (fast — no re-download)
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        "${CMAKE_EXTRA_ARGS[@]}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null 2>&1
fi

echo ""

# Determine which files to check
if [[ $# -gt 0 ]]; then
    FILES=("$@")
else
    mapfile -t FILES < <(find "${REPO_ROOT}/src" -name "*.c" -o -name "*.h")
fi

echo "Checking ${#FILES[@]} file(s)..."
echo ""

"${CLANG_TIDY}" -p "${BUILD_DIR}" "${FILES[@]}"
