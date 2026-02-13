# ZCD Roundtrip Tests

Add a second roundtrip flow that tests the .zcd (native) format as an intermediate step, ensuring no information is lost when saving/loading from the in-house format.

**Current flow (keep as-is):**
`no_cached_results.xlsx → eval → .xlsx → compare with reference`

**New flow (add):**
`no_cached_results.xlsx → eval → .zcd → reopen .zcd → .xlsx → compare with reference`

## Phase 1: Add ZCD roundtrip to run-test.sh

- [ ] 1a: Add a second test pass in `run-test.sh` that takes the evaluated workbook, saves it as `.zcd`, reopens the `.zcd`, saves as `.xlsx`, and compares against the same Excel reference — reusing the existing compare.sh infrastructure

The updated `run-test.sh` should run both flows sequentially for each category:
1. **Direct XLSX roundtrip** (existing): `cells -i <no_cache> --eval -y <tmp.xlsx>` → compare
2. **ZCD roundtrip** (new): `cells -i <no_cache> --eval -y <tmp.zcd>` then `cells -i <tmp.zcd> -y <tmp2.xlsx>` → compare

Both must pass for the category to be marked PASS. Labels in output should distinguish the two flows (e.g., "XLSX roundtrip" vs "ZCD roundtrip").
