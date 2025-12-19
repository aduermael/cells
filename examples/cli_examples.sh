#!/bin/bash
# cli_examples.sh - Example CLI commands for the cells tool
#
# This script demonstrates common use cases for the cells CLI.
# Run individual commands or use this as a reference.

set -e

CELLS_BIN="${CELLS_BIN:-bazel-bin/apps/cli/cells}"

echo "=== Cells CLI Examples ==="
echo ""

# Check if binary exists
if [ ! -f "$CELLS_BIN" ]; then
    echo "Building cells CLI..."
    bazel build //apps/cli:cells 2>/dev/null
fi

# Create temp directory for examples
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

echo "1. Create a sample CSV file"
cat > "$TMPDIR/sample.csv" << 'CSV'
Name,Sales,Commission
Alice,1000,100
Bob,1500,150
Carol,2000,200
CSV
cat "$TMPDIR/sample.csv"
echo ""

echo "2. Convert CSV to .cells format"
"$CELLS_BIN" -i "$TMPDIR/sample.csv" "$TMPDIR/sample.cells" -v --time
echo ""

echo "3. Inspect the .cells file"
"$CELLS_BIN" -I "$TMPDIR/sample.cells"
echo ""

echo "4. Convert .cells to XLSX"
"$CELLS_BIN" -i "$TMPDIR/sample.cells" "$TMPDIR/sample.xlsx" -v --time
echo ""

echo "5. Inspect the XLSX file"
"$CELLS_BIN" -I "$TMPDIR/sample.xlsx"
echo ""

echo "6. Roundtrip: XLSX back to CSV"
"$CELLS_BIN" -i "$TMPDIR/sample.xlsx" "$TMPDIR/roundtrip.csv" -v --time
echo ""

echo "7. Compare original and roundtrip CSV"
echo "Original:"
cat "$TMPDIR/sample.csv"
echo ""
echo "Roundtrip:"
cat "$TMPDIR/roundtrip.csv"
echo ""

echo "=== All examples completed successfully ==="
