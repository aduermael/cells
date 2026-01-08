// =============================================================================
// Luau Autocomplete
// =============================================================================
//
// Provides IDE-like autocomplete suggestions for Luau scripts.
// Uses Luau's Analysis library for type-aware completions.
//
// Key responsibilities:
// - Parse partial Luau code and determine cursor context
// - Provide suggestions for Cells API functions and properties
// - Support function signature hints and property documentation
// - Handle keyword, variable, and method completions
//
// Suggestion types:
// - "function": Callable functions (cellGet, print, etc.)
// - "property": Object properties (cell.value, sheet.name)
// - "keyword": Luau keywords (local, function, if, etc.)
// - "variable": In-scope variable names
//
// Context types returned:
// - "statement": Top-level statement context
// - "expression": Expression context
// - "property": After a dot (obj.xxx)
// - "type": Type annotation context
//
// Dependencies: Luau Analysis library (external)
// Used by: bindings.cc (script editor autocomplete)
//
// =============================================================================

#ifndef CELLS_LUAU_AUTOCOMPLETE_H_
#define CELLS_LUAU_AUTOCOMPLETE_H_

#include <memory>
#include <string>
#include <vector>

namespace cells {

// A single autocomplete suggestion
struct AutocompleteSuggestion {
    std::string label;        // Display text
    std::string insertText;   // Text to insert (may differ from label)
    std::string kind;         // "function", "property", "keyword", "variable", etc.
    std::string detail;       // Additional info (e.g., type signature)
    bool deprecated{false};   // Whether this suggestion is deprecated
    std::string typeCorrect;  // "correct", "correctFunctionResult", or "" (none)
};

// Result of autocomplete request
struct AutocompleteResult {
    std::vector<AutocompleteSuggestion> suggestions;
    std::string context;  // "statement", "expression", "property", "type", etc.
};

// LuauAutocomplete - provides autocomplete suggestions for Luau scripts
//
// This class wraps Luau's Analysis library to provide IDE-like autocomplete
// functionality for the Cells scripting API.
//
// Usage:
//   LuauAutocomplete autocomplete;
//   auto result = autocomplete.getCompletions(source, line, column);
//
class LuauAutocomplete {
public:
    LuauAutocomplete();
    ~LuauAutocomplete();

    // Non-copyable
    LuauAutocomplete(const LuauAutocomplete&) = delete;
    LuauAutocomplete& operator=(const LuauAutocomplete&) = delete;

    // Movable
    LuauAutocomplete(LuauAutocomplete&& other) noexcept;
    LuauAutocomplete& operator=(LuauAutocomplete&& other) noexcept;

    // Get autocomplete suggestions at the given position
    // line: 0-indexed line number
    // column: 0-indexed column number
    [[nodiscard]] AutocompleteResult getCompletions(const std::string& source, unsigned line,
                                                    unsigned column);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cells

#endif  // CELLS_LUAU_AUTOCOMPLETE_H_
