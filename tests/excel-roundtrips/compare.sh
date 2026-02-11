#!/bin/bash
set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <file1.xlsx> <file2.xlsx> [--ignore-formula-text]" >&2
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "File not found: $1" >&2
    exit 1
fi

if [ ! -f "$2" ]; then
    echo "File not found: $2" >&2
    exit 1
fi

FILE1="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
FILE2="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
EXTRA_ARGS="${@:3}"

# Build image if not found locally
if ! docker image inspect excel-evaluator &>/dev/null; then
    echo "Image 'excel-evaluator' not found. Building..." >&2
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    docker build -t excel-evaluator "$SCRIPT_DIR/evaluator"
fi

docker run --rm \
    -v "$FILE1:/data/file1.xlsx:ro" \
    -v "$FILE2:/data/file2.xlsx:ro" \
    excel-evaluator --compare /data/file1.xlsx /data/file2.xlsx $EXTRA_ARGS
