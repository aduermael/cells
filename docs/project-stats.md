# Project Stats

Generated snapshot of codebase size, test coverage, and build artifacts.

To regenerate this page and commit:

```bash
./tools/generate-stats.sh --update
```

Use `--build` to rebuild the WASM distribution before measuring sizes.

## Source Code

| Language | Lines |
|----------|------:|
| C++ | 49,321 |
| TypeScript | 24,498 |
| CSS | 2,884 |
| Starlark | 2,088 |
| Shell | 1,512 |
| Go | 1,363 |
| HTML | 1,069 |
| Objective-C++ | 1,007 |
| JavaScript | 826 |

## Test Code

| Language | Lines |
|----------|------:|
| C++ | 44,958 |
| JavaScript | 13,978 |
| C# | 1,855 |
| Go | 315 |
| Shell | 191 |

## Documentation

| Type | Lines |
|------|------:|
| Markdown | 20,377 |
| Inline Comments | 20,106 |

## Test Counts

| Category | Tests |
|----------|------:|
| Unit (C++) | 3676 |
| Unit (Go) | 13 |
| Unit (JavaScript) | 30 |
| E2E (Puppeteer) | 391 |
| Roundtrip (Excel) | 4 |
| **Total** | **4114** |

- **Commits**: 1694
- **WASM Module**: 5.46 MB
- **Total Web Bundle**: 7.64 MB

<sub>Lines counted with [CLOC](https://github.com/AlDanial/cloc) (excludes comments and blanks). Generated with `./tools/generate-stats.sh`</sub>

## LOC Evolution

<img src="../stats/loc-evolution.svg" alt="Lines of Code Evolution" width="100%">

<sub>Actual lines of code (excluding comments and blanks), tracked with [CLOC](https://github.com/AlDanial/cloc). Generate with `./tools/loc-tracker.sh && node tools/generate-loc-svg.mjs`</sub>

## Diff Size Evolution

<img src="../stats/diff-size-evolution.svg" alt="Diff Size Evolution" width="100%">

<sub>Average diff size per commit (lines added + removed, code files only). Generate with `./tools/diff-tracker.sh && node tools/generate-diff-svg.mjs`</sub>
