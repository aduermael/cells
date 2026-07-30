// =============================================================================
// WASM Bindings - Luau Scripting Operations
// =============================================================================
//
// Implementation of Luau scripting CellsEngine methods:
// - executeScript: Execute Luau scripts in sandbox
// - tokenizeLuau: Syntax highlighting tokenization
// - getAutocomplete: Code completion suggestions
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <sstream>

#include "Luau/Lexer.h"

namespace cells::wasm {

// ============================================================================
// Scripting API (Luau)
// ============================================================================

std::string CellsEngine::executeScript(const std::string& script) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return R"({"success":false,"error":"No workbook loaded"})";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (sheet == nullptr) {
        return R"({"success":false,"error":"Invalid sheet"})";
    }

    _luauSandbox.setContext(_workbook.get(), sheet);
    ScriptResult result = _luauSandbox.execute(script);

    std::ostringstream json;
    json << "{\"success\":" << (result.success ? "true" : "false");

    if (result.success) {
        json << ",\"output\":\"" << jsonEscape(result.output) << "\"";
    } else {
        json << ",\"error\":\"" << jsonEscape(result.error) << "\"";
    }

    json << ",\"instructions\":" << result.instructions;
    json << "}";

    if (result.success) {
        // Same as grid edits / CLI session exec: push CRDT ops to collab peers.
        // Without this, setCell in the script panel only mutates the local workbook.
        broadcastPendingOperations();
        rebuildViewportIndex();
        notifyListeners(ChangeType::CELL_CHANGED);
    }

    return json.str();
}

std::string CellsEngine::tokenizeLuau(const std::string& source) {
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::Lexer lexer(source.data(), source.size(), names);
    lexer.setSkipComments(false);

    std::ostringstream json;
    json << "[";
    bool first = true;

    while (true) {
        const Luau::Lexeme& lexeme = lexer.next();
        if (lexeme.type == Luau::Lexeme::Eof) {
            break;
        }

        if (!first) {
            json << ",";
        }
        first = false;

        const char* tokenType = "operator";
        if (lexeme.type >= Luau::Lexeme::Reserved_BEGIN &&
            lexeme.type < Luau::Lexeme::Reserved_END) {
            tokenType = "keyword";
        } else if (lexeme.type == Luau::Lexeme::QuotedString ||
                   lexeme.type == Luau::Lexeme::RawString ||
                   lexeme.type == Luau::Lexeme::InterpStringBegin ||
                   lexeme.type == Luau::Lexeme::InterpStringMid ||
                   lexeme.type == Luau::Lexeme::InterpStringEnd ||
                   lexeme.type == Luau::Lexeme::InterpStringSimple) {
            tokenType = "string";
        } else if (lexeme.type == Luau::Lexeme::Number) {
            tokenType = "number";
        } else if (lexeme.type == Luau::Lexeme::Comment ||
                   lexeme.type == Luau::Lexeme::BlockComment) {
            tokenType = "comment";
        } else if (lexeme.type == Luau::Lexeme::Name) {
            tokenType = "name";
        } else if (lexeme.type == Luau::Lexeme::BrokenString ||
                   lexeme.type == Luau::Lexeme::BrokenComment ||
                   lexeme.type == Luau::Lexeme::BrokenUnicode ||
                   lexeme.type == Luau::Lexeme::BrokenInterpDoubleBrace ||
                   lexeme.type == Luau::Lexeme::Error) {
            tokenType = "error";
        }

        unsigned int startOffset = 0;
        unsigned int endOffset = 0;
        unsigned int currentOffset = 0;
        unsigned int currentLine = 0;

        for (size_t i = 0; i < source.size(); ++i) {
            if (currentLine == lexeme.location.begin.line &&
                currentOffset == lexeme.location.begin.column) {
                startOffset = static_cast<unsigned int>(i);
            }
            if (currentLine == lexeme.location.end.line &&
                currentOffset == lexeme.location.end.column) {
                endOffset = static_cast<unsigned int>(i);
                break;
            }
            if (source[i] == '\n') {
                ++currentLine;
                currentOffset = 0;
            } else {
                ++currentOffset;
            }
        }

        if (endOffset == 0 && startOffset > 0) {
            endOffset = static_cast<unsigned int>(source.size());
        }

        std::string text = source.substr(startOffset, endOffset - startOffset);

        json << "{\"type\":\"" << tokenType << "\","
             << "\"text\":\"" << jsonEscape(text) << "\","
             << "\"start\":" << startOffset << ","
             << "\"end\":" << endOffset << "}";
    }

    json << "]";
    return json.str();
}

std::string CellsEngine::getAutocomplete(const std::string& source, unsigned line,
                                          unsigned column) {
    auto result = _luauAutocomplete.getCompletions(source, line, column);

    std::ostringstream json;
    json << "{\"context\":\"" << jsonEscape(result.context) << "\",";
    json << "\"suggestions\":[";

    bool first = true;
    for (const auto& suggestion : result.suggestions) {
        if (!first) {
            json << ",";
        }
        first = false;

        json << "{\"label\":\"" << jsonEscape(suggestion.label) << "\",";
        json << "\"insertText\":\"" << jsonEscape(suggestion.insertText) << "\",";
        json << "\"kind\":\"" << jsonEscape(suggestion.kind) << "\",";
        json << "\"detail\":\"" << jsonEscape(suggestion.detail) << "\",";
        json << "\"deprecated\":" << (suggestion.deprecated ? "true" : "false") << ",";
        json << "\"typeCorrect\":\"" << jsonEscape(suggestion.typeCorrect) << "\"}";
    }

    json << "]}";
    return json.str();
}

}  // namespace cells::wasm
