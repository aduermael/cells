Status: IN_PROGRESS
Created At: 2026-01-01 23:30 UTC
Updated At: 2026-01-01 22:55 UTC
Following plan management guidelines defined in AGENTS.md

## Commands

| Task | Command |
|------|---------|
| Build | `make build` |
| Unit tests | `make test` |
| E2E tests | `cd apps/wasm && npm run test:parallel -- stable` |
| Lint | `make lint` |
| Format | `make format` |
| Full check | `make check` |

# Smart Autocomplete with Type Awareness

Improve the script editor autocomplete to leverage Luau's type system for context-aware suggestions.

## Problems

1. **Enter key conflict**: Autocomplete triggers on every keystroke, making Enter hard to use for line breaks (it selects suggestion instead)

2. **No type context**: Autocomplete shows generic completions (keywords, global functions) regardless of context. For example, when typing `setCell(`, it should know the first parameter expects a `string` (cell reference). Instead, it shows `function`, `nil`, `false`, etc.

## Solution

**Phase 1**: Smarter trigger logic - only show autocomplete when useful, fix Enter behavior

**Phases 2-5**: Use Luau's `Luau::Autocomplete` module which provides:
- `ExpectedType` information at cursor position
- Context-aware filtering of suggestions
- Parameter hints showing expected types

The Luau type system can infer expected types from function signatures in our type definitions.

## Phase 1: Smarter Autocomplete Triggers

Current behavior: autocomplete shows on every keystroke, making Enter hard to use for line breaks.

**New behavior:**
- Only show autocomplete when suggestions are actually useful
- Enter should insert newline unless user is actively navigating suggestions

- [x] 1a: Only trigger autocomplete after `.` or `:` (property/method access)
- [x] 1b: Only trigger after typing 2+ identifier characters (not on first letter)
- [x] 1c: Don't trigger inside strings or comments
- [x] 1d: Don't trigger after keywords that don't expect completions (`end`, `then`, `do`, etc.)
- [x] 1e: Enter only selects suggestion if user has navigated with arrow keys; otherwise insert newline
- [x] 1f: Escape or clicking away dismisses popup without side effects

**Files:**
- `core/cells/luau_autocomplete.cc` - Smart trigger logic (1a-1d) in C++ for all clients
- `core/cells/luau_autocomplete_test.cc` - Tests for smart trigger behavior
- `apps/wasm/src/script-panel.ts` - Enter key behavior (1e), already had Escape (1f)

---

## Phase 2: Improve Type Definitions

Current type definitions may be incomplete. Ensure all API functions have proper Luau type annotations.

- [ ] 2a: Review `luau_autocomplete.cc` type definitions for completeness
- [ ] 2b: Add proper parameter types to all Cells API functions
- [ ] 2c: Add return types (`Cell`, `Sheet`, etc.) with all properties typed
- [ ] 2d: Verify type checker can infer types correctly with test cases

**Files:**
- `core/cells/luau_autocomplete.cc` - Enhance type definition source

---

## Phase 3: Expose Expected Type Information

The Luau Autocomplete API can provide `expectedType` information. Expose this to the UI.

- [ ] 3a: Research Luau's `AutocompleteResult::expectedType` field
- [ ] 3b: Include expected type info in autocomplete response JSON
- [ ] 3c: Update TypeScript types for autocomplete response
- [ ] 3d: Add C++ tests verifying expected type is returned for function arguments

**Files:**
- `core/cells/luau_autocomplete.cc` - Add expectedType to response
- `core/cells/luau_autocomplete_test.cc` - Add expected type tests
- `apps/wasm/src/client-types.ts` - Update AutocompleteResult type

---

## Phase 4: UI Enhancements for Type Context

Show type hints in the autocomplete popup and filter/prioritize suggestions based on expected type.

- [ ] 4a: Display expected type hint above/below suggestion list (e.g., "Expected: string")
- [ ] 4b: Prioritize suggestions matching expected type (e.g., string literals when string expected)
- [ ] 4c: Add parameter hints showing function signature while typing arguments
- [ ] 4d: Consider showing string placeholder/template for cell references when string expected

**Files:**
- `apps/wasm/src/script-panel.ts` - Update autocomplete UI
- `apps/wasm/static/shared/styles.css` - Styling for type hints

---

## Phase 5: Smart String Completions (Optional)

When a string is expected in a cell context, provide smart completions.

- [ ] 5a: When expected type is string and context suggests cell reference, offer column letters
- [ ] 5b: Consider sheet name completions for sheet-related string parameters
- [ ] 5c: Add completion for range syntax patterns

**Files:**
- `core/cells/luau_autocomplete.cc` - Add context-specific string completions
- `apps/wasm/src/script-panel.ts` - Handle special string completions

---

## Resources

- [Luau Autocomplete.h](https://github.com/luau-lang/luau/blob/master/Analysis/include/Luau/Autocomplete.h) - `AutocompleteResult` struct with `expectedType`
- [Luau Autocomplete.test.cpp](https://github.com/luau-lang/luau/blob/master/tests/Autocomplete.test.cpp) - Test examples showing expected type usage
- Current implementation: `core/cells/luau_autocomplete.cc`

---

## Expected Behavior After Implementation

```lua
-- Typing: setCell(|
-- Autocomplete shows: "Expected: string (cell reference)"
-- Suggestions prioritize: string variables, "A1", column names

-- Typing: setCell("A1", |
-- Autocomplete shows: "Expected: any (value)"
-- Suggestions: numbers, strings, booleans, variables

-- Typing: getSheet({name = |
-- Autocomplete shows: "Expected: string"
-- Could offer existing sheet names
```
