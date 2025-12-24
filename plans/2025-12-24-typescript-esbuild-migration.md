# Plan: Convert JS to TypeScript with esbuild

Status: IN_PROGRESS
Created At: 2025-12-24 21:06 UTC
Updated At: 2025-12-24 21:45 UTC
Following plan management guidelines defined in AGENTS.md

## Summary
Convert all JavaScript files in `apps/wasm/` to TypeScript with proper type definitions, use esbuild for bundling/minifying, and add type checking as part of the build process.

## Current State
- 11 JS files (no bundler, no TypeScript)
- Source files copied directly to `dist/` without processing
- JSDoc comments exist but no actual type checking
- `cells.d.ts` already exists with comprehensive WASM types

**Source files to convert:**
- `apps/wasm/client.js` (~957 lines) - CellsClient class
- `apps/wasm/worker.js` (~692 lines) - Web Worker
- `apps/wasm/static/shared/grid-renderer.js` (~960 lines) - GridRenderer class
- `apps/wasm/static/shared/grid-events.js` (~535 lines) - GridEventHandler class
- `apps/wasm/static/shared/utils.js` (~176 lines) - Utility functions
- `apps/wasm/static/shared/cpp-sync-adapter.js` (~720 lines) - CppSyncAdapter class
- `apps/wasm/static/shared/presence.js` (~64 lines) - Presence utilities
- `apps/wasm/static/shared/ui-state.js` (~623 lines) - UI state machine
- `apps/wasm/static/shared/collab-ui.js` (~809 lines) - CollabUI class
- `apps/wasm/static/shared/rtc-proxy.js` (~489 lines) - WebRTC proxy
- `apps/wasm/static/shared/room-url.js` (~177 lines) - Room URL management

## Target State
- TypeScript source files with proper type definitions
- esbuild bundles JS for production (minified, tree-shaken)
- `make check-types` for standalone type checking
- Type checking integrated into build

---

## Phase 1: Setup TypeScript and esbuild infrastructure
- [x] 1a: Create `apps/wasm/tsconfig.json` with strict settings
- [x] 1b: Create `apps/wasm/package.json` with TypeScript and esbuild deps
- [x] 1c: Create `apps/wasm/build.mjs` esbuild script for bundling

## Phase 2: Define core type interfaces
- [x] 2a: Create `apps/wasm/src/types.ts` with shared interfaces (Cell, Sheet, Presence, etc.)

## Phase 3: Convert utility and helper modules (no dependencies)
- [x] 3a: Convert `utils.js` to `src/utils.ts`
- [x] 3b: Convert `presence.js` to `src/presence.ts`
- [x] 3c: Convert `room-url.js` to `src/room-url.ts`

## Phase 4: Convert UI modules
- [x] 4a: Convert `ui-state.js` to `src/ui-state.ts`
- [x] 4b: Convert `grid-renderer.js` to `src/grid-renderer.ts`
- [ ] 4c: Convert `grid-events.js` to `src/grid-events.ts`

## Phase 5: Convert collaboration modules
- [ ] 5a: Convert `rtc-proxy.js` to `src/rtc-proxy.ts`
- [ ] 5b: Convert `cpp-sync-adapter.js` to `src/cpp-sync-adapter.ts`
- [ ] 5c: Convert `collab-ui.js` to `src/collab-ui.ts`

## Phase 6: Convert main application files
- [ ] 6a: Convert `client.js` to `src/client.ts`
- [ ] 6b: Convert `worker.js` to `src/worker.ts`

## Phase 7: Update build system
- [ ] 7a: Update Makefile with new targets (`check-types`, updated `wasm-dist`)
- [ ] 7b: Remove old JS files, update .gitignore
- [ ] 7c: Verify build and test end-to-end

---

## Key Design Decisions

### Directory structure
```
apps/wasm/
├── src/                    # TypeScript sources
│   ├── types.ts           # Shared type definitions
│   ├── client.ts          # CellsClient
│   ├── worker.ts          # Web Worker
│   ├── utils.ts
│   ├── presence.ts
│   ├── room-url.ts
│   ├── ui-state.ts
│   ├── grid-renderer.ts
│   ├── grid-events.ts
│   ├── rtc-proxy.ts
│   ├── cpp-sync-adapter.ts
│   └── collab-ui.ts
├── tsconfig.json
├── package.json
├── build.mjs              # esbuild script
└── static/
    └── shared/            # CSS stays here, JS deleted
```

### esbuild configuration
- Bundle client.ts and worker.ts as separate entry points
- Output to `dist/` with minification
- Target ES2020 for modern browser support
- Source maps for debugging

### Makefile targets
- `make wasm-dist` - Build WASM + bundle TypeScript
- `make check-types` - Run TypeScript type checking only (fast)
- `make wasm-serve` - Serve for local testing

### Type strategy
- **Strict mode enabled**: noImplicitAny, strictNullChecks, strictFunctionTypes, etc.
- Create typed interfaces for all data structures
- Use `unknown` with type guards rather than `any`
- Leverage existing `cells.d.ts` for WASM types

### tsconfig.json key settings
```json
{
  "compilerOptions": {
    "strict": true,
    "target": "ES2020",
    "module": "ES2020",
    "moduleResolution": "bundler",
    "esModuleInterop": true,
    "skipLibCheck": true,
    "declaration": true,
    "declarationMap": true,
    "sourceMap": true,
    "outDir": "./dist",
    "rootDir": "./src",
    "lib": ["ES2020", "DOM", "DOM.Iterable", "WebWorker"]
  }
}
```
