# Documentation Review & README Overhaul

## Goal

Review and improve all documentation for accuracy and clarity. The main README should:
- Give a clear high-level view of the stack
- "Sell" the project and look appealing
- Not go too deep into implementation details
- Not use abstract statements without explanation

## Phase 1: Rewrite Main README

- [ ] 1a: Rewrite README.md with new structure:
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

- [ ] 2a: Review and update `docs/data-model.md`
  - Remove outdated "doubly-linked dimensions" references if present
  - Verify position-based ordering explanation is accurate
  - Ensure Order Statistic Tree description is current

- [ ] 2b: Review and update `docs/crdt.md`
  - Verify operation types are current
  - Check implementation status table
  - Ensure examples match current API

- [ ] 2c: Review and update `docs/formula-engine.md`
  - Verify function count (83) is accurate
  - Check implementation status
  - Ensure examples are current

- [ ] 2d: Review and update `docs/persistence.md`
  - Verify file format specification is current
  - Check line prefix documentation matches implementation
  - Ensure examples are accurate

- [ ] 2e: Review and update `docs/networking.md`
  - Verify implementation status table
  - Check API examples match current code
  - Ensure platform support is accurate

- [ ] 2f: Review and update `docs/rendering.md`
  - Verify rendering pipeline description
  - Check constants match code
  - Ensure viewport indexing section is current

- [ ] 2g: Review and update `docs/type-system.md`
  - Update implementation status if changed
  - Verify cell value types are accurate

## Phase 3: Cleanup & Consistency

- [ ] 3a: Ensure consistent terminology across all docs
  - "Order Statistic Tree" vs "OSTree"
  - "CRDT operations" terminology
  - "UUID-based" vs "ID-based"

- [ ] 3b: Update cross-references between docs
  - Fix any broken internal links
  - Ensure referenced sections exist

- [ ] 3c: Final review pass
  - Read through all docs for flow
  - Check for any remaining overly technical language in README
  - Verify all claims match implementation
