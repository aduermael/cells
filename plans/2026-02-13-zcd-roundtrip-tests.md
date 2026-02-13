# ZCD Roundtrip Tests

Add a second roundtrip flow that tests the .zcd (native) format as an intermediate step, ensuring no information is lost when saving/loading from the in-house format.

**Current flow (keep as-is):**
`no_cached_results.xlsx → eval → .xlsx → compare with reference`

**New flow (add):**
`no_cached_results.xlsx → eval → .zcd → reopen .zcd → .xlsx → compare with reference`

### Naming conventions

Map property names to the app's existing conventions — do NOT mirror XLSX/OpenXML names blindly:

- **V line properties** (sheet-level): `camelCase` keys — e.g., `showGridLines:0`, `zoomScale:100`, `freezeCol:2`
- **Axis properties** (col/row): short lowercase keys — e.g., `w:100`, `h:24`, `hidden:1`, `name:"A"`, `sty:...`, `fmt:...`
- **CRDT ops**: reuse existing op types where possible (`SHEET_SET` for sheet props, `COL_SET`/`ROW_SET` for axis props, `WORKBOOK_SET` for workbook props) — add new payload fields rather than new op types
- **Full-state SETs**: each SET op payload must contain ALL properties of the entity, not just the changed one. This allows a SET after DELETE to fully resurrect the entity without replaying history.

## Phase 1: Add ZCD roundtrip to run-test.sh

- [x] 1a: Add a second test pass in `run-test.sh` that takes the evaluated workbook, saves it as `.zcd`, reopens the `.zcd`, saves as `.xlsx`, and compares against the same Excel reference — reusing the existing compare.sh infrastructure. Currently fails on `math-basic` due to missing sheet properties in ZCD.

The updated `run-test.sh` should run both flows sequentially for each category:
1. **Direct XLSX roundtrip** (existing): `cells -i <no_cache> --eval -y <tmp.xlsx>` → compare
2. **ZCD roundtrip** (new): `cells -i <no_cache> --eval -y <tmp.zcd>` then `cells -i <tmp.zcd> -y <tmp2.xlsx>` → compare

Both must pass for the category to be marked PASS. Labels in output should distinguish the two flows (e.g., "XLSX roundtrip" vs "ZCD roundtrip").

## Phase 2: Serialize sheet properties to ZCD

The ZCD roundtrip currently loses these sheet-level properties. Add serialization, parsing, and extend `SHEET_SET` payload for each.

Properties on the `V` line (camelCase, consistent with `showGridLines`/`zoomScale`/etc.):

- [x] 2a: `defaultRowHeight` — serialize as `V defaultRowHeight:<double>` on the V line. Uses `std::to_chars` for shortest exact representation. SHEET_SET payload extension deferred (existing view props like showGridLines don't use it either).
- [x] 2b: `pageMargins` — serialize as `V pageMargins:<left>,<right>,<top>,<bottom>,<header>,<footer>` on the V line. Parser handles comma-separated doubles via `strtod`. SHEET_SET payload extension deferred.

After this phase, re-run roundtrip test to check progress.

## Phase 3: Serialize axis `sizeOriginal` to ZCD

The XLSX writer uses `sizeOriginal` (original Excel-unit widths/heights) for lossless column/row sizing. Currently only the pixel-based `size` is serialized via `w:`/`h:`.

- [x] 3a: Add `sizeOriginal` as a short key on axis lines — e.g., `C <id> <pos> w:100 wo:8.43` (columns), `R <id> <pos> h:24 ho:16` (rows). Extend `COL_SET`/`ROW_SET` op payloads with `"sizeOriginal":D`. Added `extractJSONDouble` to CRDT JSON utilities. Serializer writes `wo:`/`ho:`, parser reads them back, and `bootstrapOpLog` includes `sizeOriginal` in op payloads.

After this phase, re-run roundtrip test to check progress.

## Phase 4: Serialize workbook theme to ZCD

The theme (12-color scheme + font scheme) is used to resolve theme-based color references in cell styles. Without it, re-exported XLSX files get default Office theme colors instead of the originals.

- [ ] 4a: Design ZCD line format for theme data (color scheme: 12 named colors; font scheme: major/minor font names)
- [ ] 4b: Serialize theme to ZCD and parse it back, extend `WORKBOOK_SET` op payload

After this phase, re-run roundtrip test — `math-basic` should now PASS on ZCD roundtrip.
