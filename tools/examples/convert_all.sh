#!/bin/bash
# convert_all.sh - Batch convert spreadsheet files
#
# Usage:
#   ./convert_all.sh INPUT_DIR OUTPUT_DIR [FORMAT]
#
# Examples:
#   ./convert_all.sh data/ output/           # Convert to .zcd
#   ./convert_all.sh data/ csv_export/ csv   # Convert to CSV
#   ./convert_all.sh xlsx/ backup/ xlsx      # Convert to XLSX

set -e

CELLS_BIN="${CELLS_BIN:-cells}"

# Check if cells binary exists
if ! command -v "$CELLS_BIN" &> /dev/null; then
    # Try common build locations
    if [ -f "bazel-bin/apps/cli/cells" ]; then
        CELLS_BIN="bazel-bin/apps/cli/cells"
    elif [ -f "../bazel-bin/apps/cli/cells" ]; then
        CELLS_BIN="../bazel-bin/apps/cli/cells"
    else
        echo "Error: 'cells' binary not found."
        echo "Build it with: bazel build //apps/cli:cells"
        echo "Or set CELLS_BIN environment variable."
        exit 1
    fi
fi

# Parse arguments
INPUT_DIR="${1:-.}"
OUTPUT_DIR="${2:-./converted}"
FORMAT="${3:-zcd}"

# Validate format
case "$FORMAT" in
    zcd|csv|xlsx) ;;
    *)
        echo "Error: Invalid format '$FORMAT'. Use: zcd, csv, xlsx"
        exit 1
        ;;
esac

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Count files
COUNT=0
FAILED=0

echo "Converting files from $INPUT_DIR to $OUTPUT_DIR (format: $FORMAT)"
echo ""

# Convert each supported file
shopt -s nullglob
for file in "$INPUT_DIR"/*.csv "$INPUT_DIR"/*.tsv "$INPUT_DIR"/*.xlsx "$INPUT_DIR"/*.zcd; do
    [ -f "$file" ] || continue

    filename=$(basename "$file")
    name="${filename%.*}"
    output="$OUTPUT_DIR/${name}.${FORMAT}"

    echo -n "  $filename -> ${name}.${FORMAT} ... "

    if "$CELLS_BIN" -i "$file" "$output" -y -q 2>/dev/null; then
        echo "OK"
        COUNT=$((COUNT + 1))
    else
        echo "FAILED"
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "Done: $COUNT converted, $FAILED failed"
