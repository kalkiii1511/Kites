#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${KITES_BUILD_DIR:-$ROOT_DIR/build/web_api}"
VENV_DIR="${KITES_VENV_DIR:-$ROOT_DIR/.venv}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required to build kites-cli." >&2
    exit 1
fi

QT_PREFIX="${CMAKE_PREFIX_PATH:-}"
if [[ -z "$QT_PREFIX" ]] && command -v brew >/dev/null 2>&1 && brew --prefix qt >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt)"
fi
if [[ -z "$QT_PREFIX" ]]; then
    echo "Qt 6.9.2+ was not found. Install Qt, then set CMAKE_PREFIX_PATH to its installation prefix." >&2
    echo "On macOS with Homebrew: brew install qt" >&2
    exit 1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DDISABLE_GUI=ON
cmake --build "$BUILD_DIR" --target kites_cli --parallel 2

if [[ ! -x "$VENV_DIR/bin/python" ]]; then
    python3 -m venv "$VENV_DIR"
fi
"$VENV_DIR/bin/python" -m pip install -r "$ROOT_DIR/web_api/requirements.txt"

cd "$ROOT_DIR"
export KITES_CLI="$BUILD_DIR/kites-cli"
exec "$VENV_DIR/bin/python" -m uvicorn web_api.main:app --host "${KITES_API_HOST:-127.0.0.1}" --port "${KITES_API_PORT:-8000}"
