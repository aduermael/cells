// =============================================================================
// WASM Bindings - Luau Scripting and AI Agent Operations
// =============================================================================
//
// Implementation of Luau scripting and AI agent CellsEngine methods:
// - executeScript: Execute Luau scripts in sandbox
// - tokenizeLuau: Syntax highlighting tokenization
// - getAutocomplete: Code completion suggestions
// - AgentClientDelegate implementation: Callbacks from AgentClient
// - initAgent/sendAgentMessage: AI agent interaction
// - feedAgentStreamData/endAgentStream: JS-based streaming
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <sstream>

#include "Luau/Lexer.h"

namespace cells::wasm {

// ============================================================================
// Helper to parse JSON string values
// ============================================================================

static std::string parseAgentJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos += searchKey.length();

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += json[pos];
                    break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

// ============================================================================
// AgentClientDelegate implementation
// ============================================================================

void CellsEngine::onAgentText(const std::string& text) {
    if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
        _agentListener(std::string("text"), text);
    }
}

void CellsEngine::onAgentToolUse(const std::string& toolId, const std::string& name,
                                  const std::string& inputJson) {
    if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
        std::ostringstream json;
        json << "{\"id\":\"" << jsonEscape(toolId) << "\",";
        json << "\"name\":\"" << jsonEscape(name) << "\",";
        json << "\"input\":" << inputJson << "}";
        _agentListener(std::string("tool_use"), json.str());
    }
}

void CellsEngine::onAgentToolResultNeeded(const std::string& toolUseId) {
    if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
        _agentListener(std::string("tool_result_needed"), toolUseId);
    }
}

void CellsEngine::onAgentComplete(const std::string& stopReason,
                                   const std::string& conversationId) {
    if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
        std::ostringstream json;
        json << "{\"stop_reason\":\"" << jsonEscape(stopReason) << "\",";
        json << "\"conversation_id\":\"" << jsonEscape(conversationId) << "\"}";
        _agentListener(std::string("done"), json.str());
    }
}

void CellsEngine::onAgentError(const std::string& message) {
    if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
        _agentListener(std::string("error"), message);
    }
}

// ============================================================================
// Agent API methods
// ============================================================================

void CellsEngine::setAgentListener(val callback) {
    _agentListener = callback;
}

void CellsEngine::removeAgentListener() {
    _agentListener = val::null();
}

void CellsEngine::initAgent(const std::string& serverUrl) {
    _agentServerUrl = serverUrl;

    AgentClientConfig config;
    config.serverUrl = serverUrl;
    config.autoExecuteTools = true;

    _agentClient = std::make_unique<AgentClient>(config, this);

    if (_workbook && _activeSheetIndex < _workbook->sheetCount()) {
        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (sheet) {
            _agentClient->setContext(_workbook.get(), sheet, &_luauSandbox);
        }
    }
}

std::string CellsEngine::getAgentServerUrl() const {
    return _agentServerUrl;
}

void CellsEngine::feedAgentStreamData(const std::string& data) {
    if (!_agentSseParser) {
        _agentSseParser = std::make_unique<net::SSEParser>(
            [this](const std::string& eventType, const std::string& eventData) {
                handleAgentSSEEvent(eventType, eventData);
            });
    }
    _agentSseParser->feed(data);
}

void CellsEngine::endAgentStream() {
    _agentSseParser.reset();
    _agentIsStreaming = false;
}

void CellsEngine::errorAgentStream(const std::string& error) {
    _agentSseParser.reset();
    _agentIsStreaming = false;
    onAgentError(error);
}

bool CellsEngine::isAgentStreaming() const {
    return _agentIsStreaming;
}

void CellsEngine::setAgentStreaming(bool streaming) {
    _agentIsStreaming = streaming;
}

void CellsEngine::handleAgentSSEEvent(const std::string& eventType, const std::string& data) {
    if (eventType == "text") {
        std::string text = parseAgentJsonString(data, "text");
        onAgentText(text);
    } else if (eventType == "tool_use") {
        std::string toolId = parseAgentJsonString(data, "id");
        std::string toolName = parseAgentJsonString(data, "name");

        size_t inputPos = data.find("\"input\":");
        std::string inputJson = "{}";
        if (inputPos != std::string::npos) {
            size_t bracePos = data.find('{', inputPos);
            if (bracePos != std::string::npos) {
                int depth = 1;
                size_t endPos = bracePos + 1;
                while (endPos < data.size() && depth > 0) {
                    if (data[endPos] == '{')
                        ++depth;
                    else if (data[endPos] == '}')
                        --depth;
                    ++endPos;
                }
                inputJson = data.substr(bracePos, endPos - bracePos);
            }
        }

        _pendingToolId = toolId;
        _pendingToolName = toolName;
        _pendingToolInput = inputJson;

        onAgentToolUse(toolId, toolName, inputJson);
    } else if (eventType == "tool_result_needed") {
        std::string toolUseId = parseAgentJsonString(data, "tool_use_id");
        std::string convId = parseAgentJsonString(data, "conversation_id");
        if (!convId.empty()) {
            _agentConversationId = convId;
        }
        onAgentToolResultNeeded(toolUseId);
        _needsToolExecution = true;
    } else if (eventType == "done") {
        std::string stopReason = parseAgentJsonString(data, "stop_reason");
        std::string convId = parseAgentJsonString(data, "conversation_id");
        if (!convId.empty()) {
            _agentConversationId = convId;
        }

        if (_needsToolExecution && !_pendingToolId.empty() && _workbook) {
            _needsToolExecution = false;
            executeAgentTool();
            return;
        }

        onAgentComplete(stopReason, _agentConversationId);
        _pendingToolId.clear();
        _pendingToolName.clear();
        _pendingToolInput.clear();
        _needsToolExecution = false;
    } else if (eventType == "error") {
        std::string message = parseAgentJsonString(data, "message");
        onAgentError(message);
    }
}

void CellsEngine::executeAgentTool() {
    if (_pendingToolName != "execute_code") {
        if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
            std::ostringstream json;
            json << "{\"tool_use_id\":\"" << jsonEscape(_pendingToolId) << "\",";
            json << "\"result\":\"Unknown tool: " << jsonEscape(_pendingToolName) << "\",";
            json << "\"is_error\":true}";
            _agentListener(std::string("send_tool_result"), json.str());
        }
        return;
    }

    std::string code = parseAgentJsonString(_pendingToolInput, "code");

    if (code.empty()) {
        if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
            std::ostringstream json;
            json << "{\"tool_use_id\":\"" << jsonEscape(_pendingToolId) << "\",";
            json << "\"result\":\"No code provided\",";
            json << "\"is_error\":true}";
            _agentListener(std::string("send_tool_result"), json.str());
        }
        return;
    }

    if (_workbook && _activeSheetIndex < _workbook->sheetCount()) {
        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (sheet) {
            _luauSandbox.setContext(_workbook.get(), sheet);
        }
    }

    ScriptResult result = _luauSandbox.execute(code);
    rebuildViewportIndex();

    if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
        std::ostringstream json;
        json << "{\"tool_use_id\":\"" << jsonEscape(_pendingToolId) << "\",";
        json << "\"result\":\""
             << jsonEscape(result.success ? (result.output.empty() ? "(no output)" : result.output)
                                          : result.error)
             << "\",";
        json << "\"is_error\":" << (result.success ? "false" : "true") << "}";
        _agentListener(std::string("send_tool_result"), json.str());
    }

    notifyListeners(ChangeType::CELL_CHANGED);
}

bool CellsEngine::isAgentInitialized() const {
    return _agentClient != nullptr;
}

void CellsEngine::sendAgentMessage(const std::string& prompt, const std::string& conversationId) {
    if (!_agentClient) {
        if (!_agentListener.isNull() && !_agentListener.isUndefined()) {
            _agentListener(std::string("error"), std::string("Agent not initialized"));
        }
        return;
    }

    if (_workbook && _activeSheetIndex < _workbook->sheetCount()) {
        auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
        if (sheet) {
            _agentClient->setContext(_workbook.get(), sheet, &_luauSandbox);
        }
    }

    _agentClient->sendMessage(prompt, conversationId);
}

std::string CellsEngine::getAgentConversationId() const {
    if (!_agentConversationId.empty()) {
        return _agentConversationId;
    }
    if (_agentClient) {
        return _agentClient->getConversationId();
    }
    return "";
}

void CellsEngine::clearAgentConversation() {
    _agentConversationId.clear();
    if (_agentClient) {
        _agentClient->clearConversation();
    }
}

void CellsEngine::cancelAgent() {
    if (_agentClient) {
        _agentClient->cancel();
    }
}

bool CellsEngine::isAgentProcessing() const {
    return _agentClient && _agentClient->isProcessing();
}

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
