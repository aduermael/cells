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

- [ ] 1a: Review and update `README.md` - verify architecture diagram accuracy, update stats, remove future items
- [ ] 1b: Review and update `GETTING_STARTED.md` - verify build commands work, update CLI examples
- [ ] 1c: Review and update `AGENTS.md` - verify guidelines match current workflow

## Phase 2: Data Model and CRDT Documentation

- [ ] 2a: Review `docs/data-model.md` - verify types against `core/cells/types.h`, `model.h`, update diagrams
- [ ] 2b: Review `docs/crdt.md` - verify operations against `crdt.cc/h`, update operation list and diagrams

## Phase 3: Formula Engine Documentation

- [ ] 3a: Review `docs/formula-engine.md` - verify against `formula_*.cc/h`, update function list, AST diagrams

## Phase 4: Persistence and File Format Documentation

- [ ] 4a: Review `docs/file-format.md` - verify ZCD format against `parser.cc/h`, `serializer.cc/h`
- [ ] 4b: Review `docs/persistence.md` - verify file format support, remove outdated options

## Phase 5: Networking and Sync Documentation

- [ ] 5a: Review `docs/sync-protocol.md` - verify protocol against `core/net/` implementation
- [ ] 5b: Review `docs/networking.md` - verify WebRTC implementation details

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
