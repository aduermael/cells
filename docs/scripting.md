# Scripting (Luau + Office.js)

Luau is the **only** UI mutation surface. Spreadsheet logic still lives in C++17;
scripts do not replace the formula engine.

CLI `-e` / `--script` can also run **Office.js Excel add-in** scripts in an
embedded **QuickJS** VM (not the browser engine). `.js` / `.mjs` files and
inline source containing `Excel.run` / `Office.onReady` select JavaScript;
`.luau` / `.lua` and other inline scripts stay on the Luau path. JavaScript is
an additional interface — it does not replace Luau.

Office.js host: `core/cells/js_sandbox.h` (`JsSandbox::execute`). Tests:
`bazel run :officejs` (or `bazel run :officejs -- WriteValues` for a specific test).

The host is a **subset** of Excel’s add-in API. Known correctness bugs (formulas
getter, xlsx sparse rows, xlsx cross-sheet `#REF!`) and missing APIs are tracked
in [officejs.md](./officejs.md). Fixture scripts that should pass:
[`testdata/officejs/`](../testdata/officejs/).

## Architecture contract

**The UI must not mutate the workbook except by executing Luau. There is no UI
path around the scripting runtime straight to the model.**

| Path | Goes through Luau? | Why |
|------|:------------------:|-----|
| UI writes (cell value, format/style, structure) | **Yes** | `executeUiMutation` → `LuauSandbox::execute`. Lua APIs (`setCell`, `setFormat`, …) apply CRDT ops. |
| Formula evaluation (`SUM`, `SUMIF`, …) | **No** | Native C++ AST + `FunctionRegistry`. Not transpiled to Lua. |
| Viewport / read queries | **No** | Canvas hot path; reads are not mutations. |
| Remote CRDT apply | **No** | Not UI. Peers send ops; `applyRemoteOperation` applies them. |
| File load / import | **No** | Not UI. Loading a document is not a bypass of the scripting runtime. |

CLI `-e` / `--script` and the in-app script panel use the same sandbox. The WASM
UI does not call `applyOperation` for local edits; it executes Luau (high-level
APIs such as `setCell`, or `_applyUiOp()` for structure ops that have no
dedicated Lua helper). `_applyUiOp` is still Luau execution — it is not a
skip-sandbox write.

Core entry: `core/cells/ui_mutation.h` (`executeUiMutation`, `uiWriteCell`,
`uiApplyOperation`).

Language and API reference: [skill/SCRIPTING.md](../skill/SCRIPTING.md).
Formula engine (native): [formula-engine.md](./formula-engine.md).
