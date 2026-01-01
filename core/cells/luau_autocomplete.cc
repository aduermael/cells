#include "luau_autocomplete.h"

#include <Luau/Autocomplete.h>
#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>
#include <Luau/ToString.h>

#include <optional>
#include <unordered_map>

namespace cells {

namespace {

// Module name for the user's script
constexpr const char* kMainModule = "MainModule";

// Type definitions for Cells API
// This defines the types that Luau's analysis will use for autocomplete
constexpr const char* kCellsApiTypes = R"(
-- Cell object returned by getCell()
export type Cell = {
    value: number | string | boolean | nil,
    formula: string?,
    ref: string,
}

-- Sheet object returned by getSheet()
export type Sheet = {
    name: string,
}

-- Options for getCell
export type GetCellOptions = {
    create: boolean?,
}

-- Options for getSheet
export type GetSheetOptions = {
    name: string?,
    index: number?,
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

-- Structure
declare function setColumnWidth(col: string, opts: SizeOptions): ()
declare function setRowHeight(row: number, opts: SizeOptions): ()
declare function moveColumn(col: string, opts: MoveOptions): ()

-- Sheets
declare function getSheet(opts: GetSheetOptions): Sheet?
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
        Luau::registerBuiltinGlobals(*frontend_, frontend_->globals, /* typeCheckForAutocomplete= */ false);
        Luau::registerBuiltinGlobals(*frontend_, frontend_->globalsForAutocomplete, /* typeCheckForAutocomplete= */ true);

        // Load our custom Cells API type definitions into both globals
        frontend_->loadDefinitionFile(
            frontend_->globals,
            frontend_->globals.globalScope,
            kCellsApiTypes,
            "@cells",
            /* captureComments= */ false,
            /* typeCheckForAutocomplete= */ false);

        frontend_->loadDefinitionFile(
            frontend_->globalsForAutocomplete,
            frontend_->globalsForAutocomplete.globalScope,
            kCellsApiTypes,
            "@cells",
            /* captureComments= */ false,
            /* typeCheckForAutocomplete= */ true);

        // Freeze the global types so they can't be modified
        Luau::freeze(frontend_->globals.globalTypes);
        Luau::freeze(frontend_->globalsForAutocomplete.globalTypes);
    }

    AutocompleteResult getCompletions(const std::string& source, unsigned line, unsigned column) {
        AutocompleteResult result;

        // Update source in file resolver
        fileResolver_.setSource(source);

        // Mark module as dirty to reparse
        frontend_->markDirty(kMainModule);

        // Check the module (required before autocomplete)
        Luau::FrontendOptions checkOpts;
        checkOpts.forAutocomplete = true;
        frontend_->check(kMainModule, checkOpts);

        // Get autocomplete suggestions
        Luau::Position pos{line, column};
        auto ac = Luau::autocomplete(*frontend_, kMainModule, pos, nullCallback);

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

        // Convert suggestions
        result.suggestions.reserve(ac.entryMap.size());
        for (const auto& [name, entry] : ac.entryMap) {
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

            result.suggestions.push_back(std::move(suggestion));
        }

        return result;
    }

private:
    static std::optional<Luau::AutocompleteEntryMap> nullCallback(
        std::string /*tag*/,
        std::optional<const Luau::ExternType*> /*externType*/,
        std::optional<std::string> /*contents*/) {
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

AutocompleteResult LuauAutocomplete::getCompletions(const std::string& source,
                                                    unsigned line,
                                                    unsigned column) {
    return impl_->getCompletions(source, line, column);
}

}  // namespace cells
