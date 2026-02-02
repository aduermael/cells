# Documentation Review & README Overhaul

## Goal

Review and improve all documentation for accuracy and clarity. The main README should:
- Give a clear high-level view of the stack
- "Sell" the project and look appealing
- Not go too deep into implementation details
- Not use abstract statements without explanation

## Phase 1: Rewrite Main README

- [x] 1a: Rewrite README.md with new structure:
  - **Opening pitch**: What is Cells? A modern spreadsheet engine with CLI tools and a web UI
  - **Key features section**: Highlight what makes it special (real-time collaboration, Excel compatibility, CLI tools, web UI)
  - **Screenshots/demo section**: Link to live demo or add screenshot placeholder
  - **Quick start section**: How to try it (build commands, web UI)
  - **Architecture overview**: Keep but simplify - focus on high-level components, remove low-level details like "doubly-linked dimensions with gaps"
  - **CLI section**: Highlight the CLI as a first-class citizen (format conversion, file inspection, sync)
  - **Web UI section**: Highlight as Excel/Google Sheets alternative
  - **Stats section**: Move to bottom but keep (it's impressive)
  - **Remove or relocate**: "Why UUID-based cells", "Why native AST execution", "Why doubly-linked dimensions" - these are too deep for README
  - **Remove or reword**: Abstract phrases like "Git-friendly persistence" without context

## Phase 2: Review Individual Docs

- [x] 2a: Review and update `docs/data-model.md`
  - Remove outdated "doubly-linked dimensions" references if present → none found
  - Verify position-based ordering explanation is accurate → verified against model.h (Axis.position field)
  - Ensure Order Statistic Tree description is current → verified against ostree.h, viewport_index.h

- [x] 2b: Review and update `docs/crdt.md`
  - Verify operation types are current → Updated to unified SET+DELETE pattern
  - Check implementation status table → Updated source file list
  - Ensure examples match current API → ✓

- [x] 2c: Review and update `docs/formula-engine.md`
  - Verify function count (83) is accurate → ✓ confirmed via registerFunction grep
  - Check implementation status → ✓ all marked implemented
  - Ensure examples are current → ✓

- [x] 2d: Review and update `docs/persistence.md`
  - Verify file format specification is current → ✓
  - Check line prefix documentation matches implementation → ✓ verified against parser.cc
  - Ensure examples are accurate → ✓

- [x] 2e: Review and update `docs/networking.md`
  - Verify implementation status table → ✓ all source files exist
  - Check API examples match current code → ✓
  - Ensure platform support is accurate → ✓ web/apple/native all verified

- [x] 2f: Review and update `docs/rendering.md`
  - Verify rendering pipeline description → ✓ comprehensive
  - Check constants match code → ✓ verified against grid-constants.ts
  - Ensure viewport indexing section is current → ✓ all source files verified

- [x] 2g: Review and update `docs/type-system.md`
  - Update implementation status if changed → removed outdated date
  - Verify cell value types are accurate → ✓ matches types.h

## Phase 3: Cleanup & Consistency

- [x] 3a: Ensure consistent terminology across all docs
  - "Order Statistic Tree" vs "OSTree" → Standardized to "Order Statistic Tree" (no hyphen) with "OSTree" abbreviation
  - "CRDT operations" terminology → Already consistent across docs
  - "UUID-based" vs "ID-based" → Intentionally different: UUID-based for data model, ID-based for file format (8-char short IDs)

- [x] 3b: Update cross-references between docs
  - All document links verified (file-format.md, sync-protocol.md, all docs/*.md files)
  - All internal section references (#anchors) verified in file-format.md
  - No broken links found

- [x] 3c: Final review pass
  - Docs have consistent structure (Overview/Status at top)
  - README claims verified: "80+ functions" is accurate (83 total)
  - No overly technical language without context remaining
