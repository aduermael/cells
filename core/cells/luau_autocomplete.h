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
