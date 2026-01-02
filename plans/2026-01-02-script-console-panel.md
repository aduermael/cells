Status: IN-PROGRESS
Created At: 2026-01-02 01:46 UTC
Updated At: 2026-01-02 02:15 UTC
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

---

## Summary

Add a console output panel to the Luau script editor that displays `print()` output. The console appears on the right side of the code editor (in the currently empty area), supports resizing via a draggable divider, and can be closed. The console automatically reopens when new print output is generated.

## Current State

- Script panel has: editor (line numbers + code area) + footer (status + run button)
- `ScriptResult` struct has `output` field (currently only used for return value)
- No `print()` function exists in the Luau sandbox
- Status footer shows execution time and errors

## Architecture

### Layout Change

```
Before:
┌──────────────────────────────────┐
│ line_nums │ code_editor          │
│           │                      │
├──────────────────────────────────┤
│ status                       Run │
└──────────────────────────────────┘

After:
┌────────────────────┬─────────────┐
│ line_nums │ code   │ Console [X] │
│           │        │ > output 1  │
│           │        │ > output 2  │
│           │        ├<resize>─────┤
├────────────────────┴─────────────┤
│ status                       Run │
└──────────────────────────────────┘
```

### Data Flow

1. Luau script calls `print("hello")`
2. C++ `luaPrint` function appends "hello\n" to `printBuffer_`
3. After execution, `printBuffer_` is copied to `ScriptResult.output`
4. WASM bindings pass `output` to TypeScript
5. TypeScript `ScriptPanel.run()` receives output, displays in console panel

---

## Phase 1: C++ Backend - Add print() function

- [x] 1a: Add printBuffer_ member and luaPrint static function to LuauSandbox
- [x] 1b: Register print() global in initState(), copy buffer to result.output
- [x] 1c: Add unit test for print() capturing output

## Phase 2: TypeScript UI - Console Panel Structure

- [x] 2a: Add console panel HTML structure and CSS styling
- [x] 2b: Add console panel elements to ScriptPanel constructor and wiring
- [x] 2c: Display print output in console panel after script execution

## Phase 3: Console Interactivity

- [x] 3a: Implement horizontal resize handle between editor and console
- [ ] 3b: Implement close button and auto-show on new output
- [ ] 3c: Add clear button and keyboard shortcut (Escape to close console)

---

## Implementation Details

### Phase 1a: printBuffer_ and luaPrint

```cpp
// luau_sandbox.h - add to private members:
std::string printBuffer_;

// luau_sandbox.cc - add static function:
static int luaPrint(lua_State* L) {
    auto* self = static_cast<LuauSandbox*>(lua_touserdata(L, lua_upvalueindex(1)));
    int nargs = lua_gettop(L);
    for (int i = 1; i <= nargs; i++) {
        if (i > 1) self->printBuffer_ += "\t";
        const char* s = luaL_tolstring(L, i, nullptr);
        if (s) self->printBuffer_ += s;
        lua_pop(L, 1);
    }
    self->printBuffer_ += "\n";
    return 0;
}
```

### Phase 1b: Register print()

```cpp
// In initState():
lua_pushlightuserdata(L_, this);
lua_pushcclosure(L_, &LuauSandbox::luaPrint, "print", 1);
lua_setglobal(L_, "print");

// In execute(), before running:
printBuffer_.clear();

// After execution, combine with return value:
if (!printBuffer_.empty()) {
    result.output = printBuffer_;
    // Append return value if any
}
```

### Phase 2a: HTML/CSS

Add console panel as sibling to editor container:
- `#script-console` - container with header, content, resize handle
- CSS: flexbox layout, `display: none` when hidden
- Dark theme colors matching editor

### Phase 2b: ScriptPanel wiring

Add new elements:
- `consoleEl: HTMLElement`
- `consoleContentEl: HTMLElement`
- `consoleCloseBtn: HTMLElement`
- `consoleClearBtn: HTMLElement`
- `consoleResizeHandle: HTMLElement`
- `consoleVisible: boolean`

### Phase 2c: Display output

In `run()`:
```typescript
if (result.output) {
    this.showConsole();
    this.appendToConsole(result.output);
}
```

### Phase 3: Interactivity

- Horizontal resize: mousedown on divider, track clientX delta
- Close button: hide console, remember preference
- Auto-show: if output exists and console closed, show it
- Clear: empty console content
- Keyboard: Escape closes console (if focused)
