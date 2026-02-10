#!/bin/bash
set -e

FORCE=false
if [ "$1" = "--force" ] || [ "$1" = "-f" ]; then
    FORCE=true
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="$SCRIPT_DIR/data"

# Build image if not found locally
if ! docker image inspect excel-evaluator &>/dev/null; then
    echo "Image 'excel-evaluator' not found. Building..." >&2
    docker build -t excel-evaluator "$SCRIPT_DIR/evaluator"
fi

# Find all .xlsx files, process each one individually
SUCCESS=0
SKIP=0
FAIL=0
while IFS= read -r src; do
    dir="$(dirname "$src")"
    base="$(basename "$src" .xlsx)"
    dest="${dir}/${base}_no_cached_results.xlsx"
    rel="${src#"$DATA_DIR"/}"

    if [ "$FORCE" = false ] && [ -f "$dest" ]; then
        SKIP=$((SKIP + 1))
        continue
    fi

    cp "$src" "$dest"

    # Convert to container-relative path
    container_path="/data/${dest#"$DATA_DIR"/}"

    if docker run --rm -v "$DATA_DIR:/data" excel-evaluator --remove-cached-results "$container_path"; then
        SUCCESS=$((SUCCESS + 1))
    else
        echo "  → Removing failed copy: ${dest#"$DATA_DIR"/}" >&2
        rm -f "$dest"
        FAIL=$((FAIL + 1))
    fi
done < <(find "$DATA_DIR" -name '*.xlsx' ! -name '*_no_cached_results.xlsx' | sort)

echo ""
echo "Done: $SUCCESS processed, $SKIP skipped, $FAIL failed"
[ "$FAIL" -eq 0 ]
