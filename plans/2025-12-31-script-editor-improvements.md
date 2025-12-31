Status: COMPLETE
Created At: 2025-12-31 00:31 UTC
Updated At: 2025-12-31 01:00 UTC
Following plan management guidelines defined in AGENTS.md

# Script Editor Improvements

Follow-up to the Luau scripting integration. Improve the script editor UX with:
- Fix copy/paste (currently blocked by global keyboard handler)
- Add Luau syntax highlighting using the C++ lexer
- Auto-indent on newline using Luau tokenizer

Note: Cmd/Ctrl+Enter already works (implemented in script-panel.ts:201-204).

## Phase 1: Fix Copy/Paste in Script Editor

The issue: Global keyboard handler in `app-events.ts:1283-1304` intercepts Cmd+C/V/X
when not in cell editing mode, but doesn't check if script editor has focus.

- [x] 1a: Add script editor focus check to clipboard shortcut handling
- [x] 1b: Test copy/paste works in script editor

**Files:**
- `apps/wasm/src/app-events.ts` - Add condition to check script editor focus
- `apps/wasm/src/script-panel.ts` - Add method to check if editor is focused

## Phase 2: Expose Luau Lexer to WASM

Create C++ bindings to expose Luau's lexer for syntax highlighting and auto-indent.

- [x] 2a: Add tokenize function to bindings.cc that returns JSON array of tokens
- [x] 2b: Add TypeScript types for Token interface
- [x] 2c: Add worker message handler for tokenize
- [x] 2d: Add client method for tokenize

Token JSON format:
```json
[
  {"type": "keyword", "text": "local", "start": 0, "end": 5},
  {"type": "name", "text": "x", "start": 6, "end": 7},
  {"type": "operator", "text": "=", "start": 8, "end": 9},
  {"type": "number", "text": "42", "start": 10, "end": 12}
]
```

Token types to expose:
- `keyword` - Reserved words (and, break, do, else, elseif, end, false, for, function, if, in, local, nil, not, or, repeat, return, then, true, until, while)
- `string` - QuotedString, RawString, InterpString*
- `number` - Number
- `comment` - Comment, BlockComment
- `name` - Identifiers
- `operator` - All operators and punctuation
- `error` - BrokenString, BrokenComment, Error, etc.

**Files:**
- `apps/wasm/bindings.cc` - Add tokenize function using Luau::Lexer
- `apps/wasm/BUILD` - Ensure luau_ast is in deps
- `apps/wasm/cells.d.ts` - Add tokenize type definition
- `apps/wasm/src/worker.ts` - Add tokenize message handler
- `apps/wasm/src/client.ts` - Add tokenize client method
- `apps/wasm/src/client-types.ts` - Add Token type

## Phase 3: Syntax Highlighting

Replace textarea with a custom editor that overlays syntax highlighting.

Approach: Use a transparent textarea over a `<pre><code>` element that displays
the highlighted code. This is a lightweight alternative to CodeMirror/Monaco.

- [x] 3a: Create HTML structure with textarea + highlighted backdrop
- [x] 3b: Add CSS for highlighting spans (keyword, string, number, comment, etc.)
- [x] 3c: Create SyntaxHighlighter class that tokenizes and renders highlighted HTML
- [x] 3d: Update ScriptPanel to sync textarea with highlighted display
- [x] 3e: Handle scroll sync between textarea and backdrop

Color scheme (dark theme, similar to VS Code):
- Keywords: `#569cd6` (blue)
- Strings: `#ce9178` (orange)
- Numbers: `#b5cea8` (light green)
- Comments: `#6a9955` (green)
- Names/identifiers: `#9cdcfe` (light blue)
- Operators: `#d4d4d4` (light gray)
- Errors: `#f44747` (red)

**Files:**
- `apps/wasm/static/index.html` - Update script editor structure
- `apps/wasm/static/shared/styles.css` - Add syntax highlighting styles
- `apps/wasm/src/syntax-highlighter.ts` (new) - SyntaxHighlighter class
- `apps/wasm/src/script-panel.ts` - Integrate syntax highlighting

## Phase 4: Auto-Indent

Add smart indentation when pressing Enter in the script editor.

Rules:
- After `then`, `do`, `function`, `else`, `elseif`, `repeat`: increase indent
- After `end`, `until`: decrease indent (already on same line)
- Maintain current indent level otherwise
- Use 2 spaces for indentation (consistent with Tab behavior)

Implementation: Use the lexer to find the last significant token on the current line.

- [x] 4a: Add getIndentForNewLine method to ScriptPanel
- [x] 4b: Handle Enter key to insert newline with proper indentation
- [x] 4c: Test indentation with various code patterns

**Files:**
- `apps/wasm/src/script-panel.ts` - Add auto-indent logic

## Implementation Notes

### Luau Lexer Usage (C++)

```cpp
#include "Luau/Lexer.h"
#include "Luau/Allocator.h"

std::string tokenize(const std::string& source) {
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::Lexer lexer(source.data(), source.size(), names);

    std::string result = "[";
    bool first = true;

    while (true) {
        Luau::Lexeme lexeme = lexer.next();
        if (lexeme.type == Luau::Lexeme::Eof) break;

        // Build JSON for each token...
    }

    result += "]";
    return result;
}
```

### Textarea + Backdrop Approach

```html
<div class="editor-container">
  <pre class="editor-backdrop"><code id="highlighted-code"></code></pre>
  <textarea id="script-editor"></textarea>
</div>
```

```css
.editor-container {
  position: relative;
}
.editor-backdrop {
  position: absolute;
  top: 0; left: 0; right: 0; bottom: 0;
  pointer-events: none;
  overflow: hidden;
}
#script-editor {
  position: relative;
  background: transparent;
  color: transparent;
  caret-color: white;
}
```

The textarea is transparent but the caret is visible. The backdrop shows the
highlighted code. Both must use identical font, padding, and line-height.
