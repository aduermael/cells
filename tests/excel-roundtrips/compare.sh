#!/bin/bash
set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <file1.xlsx> <file2.xlsx> [--config <path>] [--all]" >&2
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

# Parse remaining args for --config
CONFIG_FILE=""
EXTRA_ARGS=""
shift 2
while [ $# -gt 0 ]; do
    case "$1" in
        --config)
            if [ -z "$2" ]; then
                echo "--config requires a path argument" >&2
                exit 1
            fi
            CONFIG_FILE="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
            shift 2
            ;;
        *)
            EXTRA_ARGS="$EXTRA_ARGS $1"
            shift
            ;;
    esac
done

# Build image if not found locally
if ! docker image inspect excel-evaluator &>/dev/null; then
    echo "Image 'excel-evaluator' not found. Building..." >&2
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    docker build -t excel-evaluator "$SCRIPT_DIR/evaluator"
fi

CONFIG_MOUNT=""
CONFIG_ARGS=""
if [ -n "$CONFIG_FILE" ]; then
    CONFIG_MOUNT="-v $CONFIG_FILE:/data/config.json:ro"
    CONFIG_ARGS="--config /data/config.json"
fi

docker run --rm \
    -v "$FILE1:/data/file1.xlsx:ro" \
    -v "$FILE2:/data/file2.xlsx:ro" \
    $CONFIG_MOUNT \
    excel-evaluator --compare /data/file1.xlsx /data/file2.xlsx $CONFIG_ARGS $EXTRA_ARGS
