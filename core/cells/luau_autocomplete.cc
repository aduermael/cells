#include "luau_autocomplete.h"

#include <Luau/Autocomplete.h>
#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>
#include <Luau/ToString.h>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace cells {

namespace {

// Module name for the user's script
constexpr const char* kMainModule = "MainModule";

// Keywords after which autocomplete should NOT trigger (followed by expression)
const std::unordered_set<std::string> kNoAutocompleteAfter = {
    "end", "then", "do", "else", "elseif", "until", "in", "and", "or", "not",
};

// Check if a character is part of an identifier
inline bool isIdentifierChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Get the word being typed at the cursor position
std::string getCurrentWord(const std::string& source, size_t cursorPos) {
    if (cursorPos == 0) {
        return "";
    }

    size_t wordStart = cursorPos;
    while (wordStart > 0 && isIdentifierChar(source[wordStart - 1])) {
        wordStart--;
    }
    return source.substr(wordStart, cursorPos - wordStart);
}

// Get the previous word before the current word
std::string getPreviousWord(const std::string& source, size_t cursorPos) {
    // Find start of current word
    size_t wordStart = cursorPos;
    while (wordStart > 0 && isIdentifierChar(source[wordStart - 1])) {
        wordStart--;
    }

    // Skip whitespace
    size_t pos = wordStart;
    while (pos > 0 &&
           (source[pos - 1] == ' ' || source[pos - 1] == '\t' || source[pos - 1] == '\n')) {
        pos--;
    }

    // Find the previous word
    const size_t prevEnd = pos;
    while (pos > 0 && isIdentifierChar(source[pos - 1])) {
        pos--;
    }

    if (prevEnd == pos) {
        return "";
    }
    return source.substr(pos, prevEnd - pos);
}

// Check if cursor is right after a . or :
bool isAfterDotOrColon(const std::string& source, size_t cursorPos) {
    if (cursorPos == 0) {
        return false;
    }

    // Look for . or : before any identifier characters
    size_t pos = cursorPos - 1;

    // Skip identifier chars
    while (pos > 0 && isIdentifierChar(source[pos])) {
        pos--;
    }

    return source[pos] == '.' || source[pos] == ':';
}

// Check if cursor is inside a string (simple heuristic: count quotes on current line)
bool isInsideString(const std::string& source, size_t cursorPos) {
    // Find start of current line
    size_t lineStart = cursorPos;
    while (lineStart > 0 && source[lineStart - 1] != '\n') {
        lineStart--;
    }

    // Count unescaped quotes up to cursor
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (size_t i = lineStart; i < cursorPos; ++i) {
        const char c = source[i];
        const bool escaped = (i > 0 && source[i - 1] == '\\');

        if (escaped) {
            continue;
        }

        if (c == '"' && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
        } else if (c == '\'' && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
        }
    }

    return inSingleQuote || inDoubleQuote;
}

// Check if cursor is inside a comment (after -- on current line)
bool isInsideComment(const std::string& source, size_t cursorPos) {
    // Find start of current line
    size_t lineStart = cursorPos;
    while (lineStart > 0 && source[lineStart - 1] != '\n') {
        lineStart--;
    }

    // Look for -- before cursor (not inside a string)
    bool inString = false;
    char stringChar = 0;

    for (size_t i = lineStart; i < cursorPos; ++i) {
        const char c = source[i];

        if (!inString) {
            if (c == '"' || c == '\'') {
                inString = true;
                stringChar = c;
            } else if (c == '-' && i + 1 < cursorPos && source[i + 1] == '-') {
                return true;  // Found comment start
            }
        } else {
            if (c == stringChar && (i == 0 || source[i - 1] != '\\')) {
                inString = false;
            }
        }
    }

    return false;
}

// Type definitions for Cells API
// This defines the types that Luau's analysis will use for autocomplete
constexpr const char* kCellsApiTypes = R"(
-- Cell object returned by getCell()
export type Cell = {
    value: number | string | boolean | nil,
    formula: string?,
    ref: string,
    dependents: {Cell},
    dependencies: {Cell},
}

-- Sheet object returned by getSheet()
export type Sheet = {
    name: string,
}

-- Options for getCell
export type GetCellOptions = {
    create: boolean?,
}

-- Options for getSheet (index is 1-based)
export type GetSheetOptions = {
    name: string?,
    index: number?,  -- 1-based index
}

-- Options for setColumnWidth/setRowHeight
export type SizeOptions = {
    width: number?,
    height: number?,
}

-- Options for moveColumn
export type MoveOptions = {
    to: number?,
}

-- Options for fillRange
export type FillOptions = {
    from: string,
    to: string,
}

-- Cell access
declare function getCell(ref: string, opts: GetCellOptions?): Cell?
declare function setCell(ref: string, value: number | string | boolean | nil): ()

-- Document
declare function setDocumentTitle(title: string): ()
declare function getDocumentTitle(): string

-- Structure
declare function setColumnWidth(col: string, opts: SizeOptions): ()
declare function setRowHeight(row: number, opts: SizeOptions): ()
declare function moveColumn(col: string, opts: MoveOptions): ()

-- Sheets (index is 1-based)
declare function getSheet(opts: GetSheetOptions | string | number): Sheet?
declare function selectSheet(sheet: Sheet | string | number): ()
declare function addSheet(name: string?): Sheet

-- Ranges
declare function selectRange(range: string): ()
declare function deleteRange(range: string): ()
declare function fillRange(opts: FillOptions): ()
)";

}  // namespace

// Custom FileResolver for in-memory source
class InMemoryFileResolver : public Luau::FileResolver {
public:
    void setSource(const std::string& source) { source_ = source; }

    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
        if (name == kMainModule) {
            return Luau::SourceCode{source_, Luau::SourceCode::Type::Module};
        }
        return std::nullopt;
    }

private:
    std::string source_;
};

// Use Luau's built-in NullConfigResolver
using DefaultConfigResolver = Luau::NullConfigResolver;

// Implementation class (PIMPL)
class LuauAutocomplete::Impl {
public:
    Impl() {
        Luau::FrontendOptions options;
        options.forAutocomplete = true;
        options.retainFullTypeGraphs = true;

        frontend_ = std::make_unique<Luau::Frontend>(&fileResolver_, &configResolver_, options);

        // Register built-in types for both type checking and autocomplete
        Luau::registerBuiltinGlobals(*frontend_, frontend_->globals,
                                     /* typeCheckForAutocomplete= */ false);
        Luau::registerBuiltinGlobals(*frontend_, frontend_->globalsForAutocomplete,
                                     /* typeCheckForAutocomplete= */ true);

        // Load our custom Cells API type definitions into both globals
        frontend_->loadDefinitionFile(frontend_->globals, frontend_->globals.globalScope,
                                      kCellsApiTypes, "@cells",
                                      /* captureComments= */ false,
                                      /* typeCheckForAutocomplete= */ false);

        frontend_->loadDefinitionFile(frontend_->globalsForAutocomplete,
                                      frontend_->globalsForAutocomplete.globalScope, kCellsApiTypes,
                                      "@cells",
                                      /* captureComments= */ false,
                                      /* typeCheckForAutocomplete= */ true);

        // Freeze the global types so they can't be modified
        Luau::freeze(frontend_->globals.globalTypes);
        Luau::freeze(frontend_->globalsForAutocomplete.globalTypes);
    }

    AutocompleteResult getCompletions(const std::string& source, unsigned line, unsigned column) {
        AutocompleteResult result;

        // Calculate cursor position in source string
        size_t cursorPos = 0;
        unsigned currentLine = 0;
        for (size_t i = 0; i < source.size() && currentLine < line; ++i) {
            if (source[i] == '\n') {
                currentLine++;
            }
            cursorPos = i + 1;
        }
        cursorPos += column;
        if (cursorPos > source.size()) {
            cursorPos = source.size();
        }

        // Check if inside string or comment first (fast reject)
        if (isInsideString(source, cursorPos)) {
            result.context = "string";
            return result;  // Empty suggestions
        }
        if (isInsideComment(source, cursorPos)) {
            result.context = "comment";
            return result;  // Empty suggestions
        }

        // Smart trigger checks (before calling Luau's autocomplete)
        const bool afterDotOrColon = isAfterDotOrColon(source, cursorPos);

        // Get the prefix being typed (for filtering suggestions later)
        const std::string prefix = getCurrentWord(source, cursorPos);

        if (!afterDotOrColon) {
            // Check minimum character requirement (1+ chars unless after . or :)
            if (prefix.empty()) {
                result.context = "filtered";
                return result;  // Empty suggestions
            }

            // Check if after a keyword that doesn't expect completions
            const std::string prevWord = getPreviousWord(source, cursorPos);
            if (!prevWord.empty() && kNoAutocompleteAfter.count(prevWord) != 0u) {
                result.context = "filtered";
                return result;  // Empty suggestions
            }
        }

        // Update source in file resolver
        fileResolver_.setSource(source);

        // Mark module as dirty to reparse
        frontend_->markDirty(kMainModule);

        // Check the module (required before autocomplete)
        Luau::FrontendOptions checkOpts;
        checkOpts.forAutocomplete = true;
        frontend_->check(kMainModule, checkOpts);

        // Get autocomplete suggestions
        const Luau::Position pos{line, column};
        auto ac = Luau::autocomplete(*frontend_, kMainModule, pos, nullCallback);

        // Don't show suggestions inside strings
        if (ac.context == Luau::AutocompleteContext::String) {
            result.context = "string";
            return result;  // Empty suggestions
        }

        // Convert context
        switch (ac.context) {
            case Luau::AutocompleteContext::Statement:
                result.context = "statement";
                break;
            case Luau::AutocompleteContext::Expression:
                result.context = "expression";
                break;
            case Luau::AutocompleteContext::Property:
                result.context = "property";
                break;
            case Luau::AutocompleteContext::Type:
                result.context = "type";
                break;
            case Luau::AutocompleteContext::Keyword:
                result.context = "keyword";
                break;
            case Luau::AutocompleteContext::String:
                result.context = "string";
                break;
            default:
                result.context = "unknown";
                break;
        }

        // Convert suggestions, filtering by prefix
        result.suggestions.reserve(ac.entryMap.size());
        for (const auto& [name, entry] : ac.entryMap) {
            // Filter by prefix: only include suggestions that start with the typed text
            // Use case-sensitive matching since Luau is case-sensitive
            if (!prefix.empty() && name.substr(0, prefix.size()) != prefix) {
                continue;  // Skip suggestions that don't match the prefix
            }

            AutocompleteSuggestion suggestion;
            suggestion.label = name;
            suggestion.insertText = entry.insertText.value_or(name);
            suggestion.deprecated = entry.deprecated;

            // Convert kind
            if (entry.kind == Luau::AutocompleteEntryKind::Property) {
                suggestion.kind = "property";
            } else if (entry.kind == Luau::AutocompleteEntryKind::Binding) {
                suggestion.kind = "variable";
            } else if (entry.kind == Luau::AutocompleteEntryKind::Keyword) {
                suggestion.kind = "keyword";
            } else if (entry.kind == Luau::AutocompleteEntryKind::String) {
                suggestion.kind = "string";
            } else if (entry.kind == Luau::AutocompleteEntryKind::Type) {
                suggestion.kind = "type";
            } else if (entry.kind == Luau::AutocompleteEntryKind::Module) {
                suggestion.kind = "module";
            } else if (entry.kind == Luau::AutocompleteEntryKind::GeneratedFunction) {
                suggestion.kind = "function";
            } else if (entry.kind == Luau::AutocompleteEntryKind::RequirePath) {
                suggestion.kind = "path";
            } else {
                suggestion.kind = "text";
            }

            // Get type info if available
            if (entry.type) {
                suggestion.detail = Luau::toString(*entry.type);
            }

            // Include type correctness info for prioritization
            switch (entry.typeCorrect) {
                case Luau::TypeCorrectKind::Correct:
                    suggestion.typeCorrect = "correct";
                    break;
                case Luau::TypeCorrectKind::CorrectFunctionResult:
                    suggestion.typeCorrect = "correctFunctionResult";
                    break;
                case Luau::TypeCorrectKind::None:
                default:
                    // Leave empty string (default)
                    break;
            }

            result.suggestions.push_back(std::move(suggestion));
        }

        return result;
    }

private:
    static std::optional<Luau::AutocompleteEntryMap> nullCallback(
        const std::string& /*tag*/, std::optional<const Luau::ExternType*> /*externType*/,
        const std::optional<std::string>& /*contents*/) {
        return std::nullopt;
    }

    InMemoryFileResolver fileResolver_;
    DefaultConfigResolver configResolver_;
    std::unique_ptr<Luau::Frontend> frontend_;
};

// Public interface implementation

LuauAutocomplete::LuauAutocomplete() : impl_(std::make_unique<Impl>()) {}

LuauAutocomplete::~LuauAutocomplete() = default;

LuauAutocomplete::LuauAutocomplete(LuauAutocomplete&& other) noexcept = default;

LuauAutocomplete& LuauAutocomplete::operator=(LuauAutocomplete&& other) noexcept = default;

AutocompleteResult LuauAutocomplete::getCompletions(const std::string& source, unsigned line,
                                                    unsigned column) {
    return impl_->getCompletions(source, line, column);
}

}  // namespace cells
