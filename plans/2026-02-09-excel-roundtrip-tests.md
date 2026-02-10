# Excel Roundtrip Test Suite

Validate that the Cells formula engine produces the same results as Excel by roundtripping Excel files through the CLI.

**Status: Phase 2 complete.** Test infrastructure installed. Tests currently fail on style fidelity (theme color references resolved to hex).

## Background

The `tests/excel-roundtrips/` directory contains test Excel files organized by formula category (math-basic, math-trig, statistical, etc.). Each category has two files:
- `file.xlsx` — original with formulas and Excel-cached results
- `file_no_cached_results.xlsx` — same file with cached formula values stripped

The existing `compare.sh` script uses a Docker-containerized C# evaluator to compare two Excel files cell-by-cell. The Cells CLI supports `--eval` to evaluate formulas before export.

## Phase 1: Single-Category Test Script

- [x] 1a: Create `tests/excel-roundtrips/run-test.sh` — runs a single category test. Also fixed namespace-prefix handling in xlsx_reader.cc so the CLI can read files rewritten by the C# evaluator (which uses `x:` prefixed element names).
  - Takes a category name as argument (e.g. `math-basic`)
  - Resolves paths relative to the script's own directory
  - Verifies both `data/<category>/file.xlsx` and `data/<category>/file_no_cached_results.xlsx` exist
  - Builds the CLI if `dist/cli/cells` doesn't exist (via `bazel build //apps/cli:cells`)
  - Runs: `dist/cli/cells -i data/<cat>/file_no_cached_results.xlsx --eval -y <tmpfile>.xlsx`
  - Compares: `./compare.sh data/<cat>/file.xlsx <tmpfile>.xlsx`
  - Cleans up temp file
  - Reports PASS/FAIL, exits 0/1

## Phase 2: Multi-Category Test Runner

- [x] 2a: Create `tests/excel-roundtrips/run-all-tests.sh` — runs multiple categories
  - With args: runs only the specified categories
  - Without args: auto-discovers all categories under `data/` that have both files
  - Runs `run-test.sh` for each category
  - Prints summary table (PASS/FAIL per category, total pass/fail counts)
  - Exits non-zero if any test failed

## Technical Notes

### CLI Command

```bash
dist/cli/cells -i data/<cat>/file_no_cached_results.xlsx --eval -y /tmp/cells-roundtrip-<cat>.xlsx
```

This reads the xlsx (with formulas but no cached values), evaluates all formulas, and writes the result to a temp file.

### Key Files

| File | Purpose |
|------|---------|
| `tests/excel-roundtrips/compare.sh` | Existing — Docker-based cell-level Excel comparison |
| `tests/excel-roundtrips/run-test.sh` | New — single category test runner |
| `tests/excel-roundtrips/run-all-tests.sh` | New — multi-category runner with summary |
| `tests/excel-roundtrips/data/math-basic/` | First test category to validate |
| `apps/cli/converter.cc` | CLI converter with `--eval` formula evaluation |

### Verification

```bash
# Build CLI
bazel build //apps/cli:cells && mkdir -p dist/cli && cp bazel-bin/apps/cli/cells dist/cli/cells

# Run single test
cd tests/excel-roundtrips && ./run-test.sh math-basic

# Run all tests
./run-all-tests.sh
```
