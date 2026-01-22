# Documentation Review and Update

Review and update all documentation files to accurately reflect current codebase state. Code is the source of truth. Remove any references to future plans, legacy systems, previous architectures, or tech debt.

## Scope

**Documentation files to review:**
- `README.md` - Main project overview, architecture diagrams
- `GETTING_STARTED.md` - Build instructions, CLI usage
- `AGENTS.md` - Plan management guidelines
- `docs/data-model.md` - Data structures, UUID-based cells, OSTree
- `docs/crdt.md` - CRDT operations, collaboration
- `docs/formula-engine.md` - Parser, AST, execution, functions
- `docs/file-format.md` - ZCD format specification
- `docs/persistence.md` - File formats, git-friendly design
- `docs/sync-protocol.md` - WebRTC, P2P protocol
- `docs/networking.md` - P2P collaboration, signaling
- `docs/type-system.md` - Typing, validation
- `docs/rendering.md` - Canvas2D, virtual scrolling
- `docs/cross-platform.md` - Platform strategy, build targets
- `docs/coding-guidelines.md` - Code style, performance
- `docs/naming-conventions.md` - Naming rules

**Review criteria:**
- Verify all documented types/functions exist in code
- Ensure architecture diagrams match current implementation
- Remove future/roadmap items, legacy references, deprecated notes
- Add missing data types and relationships with clear diagrams
- Verify all code examples compile/run

---

## Phase 1: Core Documentation (README, Getting Started, Agents)

- [x] 1a: Review and update `README.md` - updated stats, fixed E2E test list, marked unimplemented features as planned
- [x] 1b: Review and update `GETTING_STARTED.md` - fixed E2E test list, updated WASM size (729KB→5.1MB), updated dist structure
- [x] 1c: Review and update `AGENTS.md` - fixed E2E test list, updated commit message guidelines to match actual practice

## Phase 2: Data Model and CRDT Documentation

- [x] 2a: Review `docs/data-model.md` - updated Cell/CellValue/CellError types, Axis flags, Sheet/Workbook fields, cell storage, added collaboration mode section
- [x] 2b: Review `docs/crdt.md` - updated all operation types with codes, fixed OpLog fields and methods, updated implementation status, simplified undo/redo section to reflect not-implemented state

## Phase 3: Formula Engine Documentation

- [x] 3a: Review `docs/formula-engine.md` - updated function count from "60+" to 83, fixed AST node types (added COLUMN_REF, ROW_REF, etc.), added complete function list by category with all 83 functions, updated reference format documentation, added file references

## Phase 4: Persistence and File Format Documentation

- [x] 4a: Review `docs/file-format.md` - fixed version header (#zcd v1), added F/N/V line types, updated error codes, fixed range flags, added missing operations
- [x] 4b: Review `docs/persistence.md` - fixed version header, updated structure, removed obsolete T/Y style lines, added N/V/RG types

## Phase 5: Networking and Sync Documentation

- [x] 5a: Review `docs/sync-protocol.md` - fixed op field name (type→op), operations batch (operations→batch), ping/pong field (timestamp→ts), added hello peer_id field, added ack message type, fixed presence format (uses null not has_* flags, integer indices not ID strings), added editing field, fixed throttle rate (30→5/sec), added peer-list/error message formats, added all operation codes, added workbook/format/named range ops
- [x] 5b: Review `docs/networking.md` - updated implementation status (all platforms now implemented), fixed JS→TS file paths, updated message protocol table with channels, fixed C++ API examples (SyncClient, SyncClientDelegate, PresenceManager), added libdatachannel to platform libraries

## Phase 6: UI and Platform Documentation

- [ ] 6a: Review `docs/rendering.md` - verify against TypeScript UI code in `apps/web/`
- [ ] 6b: Review `docs/cross-platform.md` - verify build targets, remove deprecated platforms

## Phase 7: Type System and Guidelines

- [ ] 7a: Review `docs/type-system.md` - verify against current implementation state
- [ ] 7b: Review `docs/coding-guidelines.md` - verify guidelines match current practices
- [ ] 7c: Review `docs/naming-conventions.md` - verify conventions match codebase

---

## Notes

- Each task involves: reading the doc, comparing against source code, updating content and diagrams
- All architecture diagrams use ASCII art in markdown (no external diagram files)
- Stats in README are generated via `tools/generate-stats.sh`
