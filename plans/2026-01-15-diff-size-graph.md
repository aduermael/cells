# Diff Size Evolution Graph

Add a new graph showing average diff size over time, tracking code changes only (excluding tests, docs, etc.) as a project maturity indicator.

## Phase 1: Diff Size Data Collection
- [x] 1a: Create `tools/diff-tracker.sh` to collect historical diff sizes
  - Iterate through commits (similar pattern to `loc-tracker.sh`)
  - For each commit, compute diff size (lines added + removed) using `git diff --stat`
  - Filter to only code files (exclude `*_test.cc`, `*.test.mjs`, `*.spec.ts`, `*.md`, etc.)
  - Store results in `stats/diff-history.json` with rolling average data

## Phase 2: SVG Graph Generation
- [ ] 2a: Create `tools/generate-diff-svg.mjs` to generate the diff size graph
  - Read from `stats/diff-history.json`
  - Calculate rolling average (e.g., 7-day or 10-commit window)
  - Generate SVG similar to `loc-evolution.svg` showing avg diff size over time
  - Output to `stats/diff-size-evolution.svg`

## Phase 3: Integration and Path Fix
- [ ] 3a: Update `tools/generate-stats.sh` to include diff size graph in output
  - Add calls to `diff-tracker.sh` and `generate-diff-svg.mjs` in `--update` mode
  - Add the new graph reference to the generated README section
- [ ] 3b: Fix the two `./scripts/generate-stats.sh` references to `./tools/generate-stats.sh`
  - Line 278: Update the `<sub>` footer text
  - Line 343: Update the CLI hint at end of script
