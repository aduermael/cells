# cells-verify

Developer CLI to generate **Excel goldens**: open an `.xlsx` in Microsoft Excel and save it again.

v1 is **Windows-only**. Goldens are produced via Excel COM (`Excel.Application`). Do not generate goldens on macOS — Excel for Mac serializes OOXML differently.

## Requirements

- Windows
- Microsoft Excel (desktop)
- Go 1.22+

Off Windows, `excel-save` exits with:

```
cells-verify: excel-save requires Windows + Excel (COM)
```

## Build

From this directory:

```bash
go build -o cells-verify ./cmd/cells-verify
```

From the repo root (after the Bazel wrapper lands):

```bash
bazel run :excel-verify -- excel-save testdata/xlsx/simple.xlsx golden.xlsx
```

## Commands

```bash
# Open input in Excel, Save As xlsx to output
cells-verify excel-save <input.xlsx> <output.xlsx>

# Tool version (on Windows, later also prints Excel version)
cells-verify version
```

Example:

```bash
cells-verify excel-save testdata/xlsx/simple.xlsx tests/excel-verify/cases/open-save-simple/golden.xlsx
```

CI never runs Excel. A Windows machine with Excel generates goldens; those files are committed and compared later (not in v1).
