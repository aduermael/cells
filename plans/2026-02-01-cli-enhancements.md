# CLI Enhancements: Scripting, Evaluation, and Empty Workbook Creation

Enhance the CLI with three capabilities:
1. **`--eval`** - Evaluate formulas when exporting XLSX → CSV (use calc engine)
2. **`--script`** - Run a Luau script from file or inline string
3. **Optional input** - Allow creating empty workbooks or running scripts without input

## Phase 1: Add `--eval` Flag for Formula Evaluation

When converting XLSX to CSV, formulas are currently exported as-is (computed values from XLSX). Add `--eval` to recalculate formulas using the Cells calculation engine before export.

- [x] 1a: Add `evaluate_formulas` bool to `Options` struct in `options.h`
- [ ] 1b: Add `--eval` argument parsing in `parse_args()` in `main.cc`
- [ ] 1c: Implement formula evaluation in converter before CSV write
- [ ] 1d: Add unit tests for `--eval` flag

**Files:**
- `apps/cli/options.h` - Add `evaluate_formulas` to `Options`
- `apps/cli/main.cc` - Parse `--eval`, update help text
- `apps/cli/converter.cc` - Evaluate formulas when flag is set
- `apps/cli/converter_test.cc` - Test formula evaluation

## Phase 2: Add `--script` Option for Luau Script Execution

Allow running a Luau script either from a file or inline. The script runs after loading input (if any) and before writing output (if any).

- [ ] 2a: Add `script_file` and `script_inline` to `Options` struct
- [ ] 2b: Add `--script` and `-e` argument parsing in `main.cc`
- [ ] 2c: Add LuauSandbox integration in converter or main
- [ ] 2d: Update help text with script examples
- [ ] 2e: Add unit tests for script execution

**Files:**
- `apps/cli/options.h` - Add script options
- `apps/cli/main.cc` - Parse `--script <file>`, `-e "<code>"`
- `apps/cli/converter.cc` - Execute script after load, before write
- `apps/cli/BUILD` - Add `//core/cells:luau_sandbox` dependency
- `apps/cli/converter_test.cc` - Test script execution

## Phase 3: Make Input File Optional

Allow creating empty workbooks when no input is specified. Useful for:
- Creating new files from scratch with scripts
- Generating empty templates
- Running scripts that create data programmatically

- [ ] 3a: Update `validate_options()` to allow missing input when output is specified
- [ ] 3b: Create empty workbook in converter when no input file
- [ ] 3c: Allow script-only mode (no input, no output, just run script)
- [ ] 3d: Update help text to reflect optional input
- [ ] 3e: Add tests for empty workbook creation and script-only mode

**Files:**
- `apps/cli/main.cc` - Update validation logic
- `apps/cli/converter.cc` - Handle empty workbook case
- `apps/cli/converter_test.cc` - Test empty workbook scenarios

## Phase 4: Update README with CLI Examples

Add comprehensive examples to the main README showing all three features.

- [ ] 4a: Add `--eval` examples to README CLI section
- [ ] 4b: Add `--script` and `-e` examples to README CLI section
- [ ] 4c: Add empty workbook creation examples to README CLI section

**Files:**
- `README.md` - Add CLI examples section

---

## CLI Usage (Target)

```bash
# Evaluate formulas before CSV export
cells -i budget.xlsx budget.csv --eval

# Run a script file
cells -i data.xlsx output.zcd --script transform.luau

# Run inline script
cells -i data.csv output.xlsx -e 'cellSet("A1", "Hello")'

# Create empty workbook
cells output.zcd
cells output.xlsx

# Script-only (no output file)
cells -i data.xlsx --script analyze.luau

# Create from scratch with script
cells output.xlsx -e 'cellSet("A1", 100); cellSet("A2", "=A1*2")'
```

## Implementation Notes

### Formula Evaluation
- Use `FormulaEvaluator` to compute cell values
- Apply to all formula cells before CSV export
- Works with XLSX → CSV conversion (most common use case)

### Script Execution Order
1. Load input file (if specified) or create empty workbook
2. Run script (if specified)
3. Write output file (if specified)

### Empty Workbook
- Create `Workbook` with default name "Untitled"
- Add one sheet named "Sheet1"
- All standard operations work as normal
