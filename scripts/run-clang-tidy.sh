#!/usr/bin/env bash
# run-clang-tidy.sh — Run clang-tidy locally using the project's .clang-tidy config.
#
# Usage:
#   ./scripts/run-clang-tidy.sh              # check all src/ files
#   ./scripts/run-clang-tidy.sh src/radio-output.c   # check one file
#
# Requirements (macOS):  brew install llvm bear
# Requirements (Linux):  apt-get install clang-tidy
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
# macOS: Xcode generator (required by OBS) does not support
#        CMAKE_EXPORT_COMPILE_COMMANDS.  We use 'bear' to intercept compiler
#        calls during a real build and produce the database that way.
#        First run is slow; subsequent runs skip the build step.
#
# Linux: standard cmake flag works fine with the Makefile/Ninja generator.
# ---------------------------------------------------------------------------
if [[ "$(uname)" == "Darwin" ]]; then
    if [[ ! -f "${COMPILE_DB}" ]]; then
        if ! command -v bear &>/dev/null; then
            echo "error: 'bear' is required for local clang-tidy on macOS." >&2
            echo "Install it with: brew install bear" >&2
            exit 1
        fi

        echo "compile_commands.json not found — configuring and building (first run is slow)..."
        cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Xcode

        # Build with bear to capture compile commands.
        # Signing disabled so the local build doesn't fail without a cert.
        XCODEPROJ="$(find "${BUILD_DIR}" -maxdepth 1 -name "*.xcodeproj" | head -1)"
        if [[ -z "${XCODEPROJ}" ]]; then
            echo "error: no .xcodeproj found in ${BUILD_DIR}" >&2
            exit 1
        fi

        bear --output "${COMPILE_DB}" -- \
            xcodebuild \
                -project "${XCODEPROJ}" \
                -configuration RelWithDebInfo \
                CODE_SIGN_IDENTITY="" \
                CODE_SIGNING_REQUIRED=NO \
                CODE_SIGNING_ALLOWED=NO \
                -quiet
        echo "compile_commands.json generated."
    else
        echo "Using existing compile_commands.json (delete build-tidy/ to rebuild)."
    fi
else
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
