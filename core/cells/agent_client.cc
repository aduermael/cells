// Agent client implementation
// Handles HTTP streaming to the agent server and SSE parsing

#include "core/cells/agent_client.h"

#include <cstring>

#include "core/cells/luau_sandbox.h"
#include "core/cells/model.h"
#include "core/net/include/HttpRequest.h"
#include "core/net/include/SSEParser.h"
#include "core/net/include/URL.h"

namespace cells {

AgentClient::AgentClient(AgentClientConfig config, AgentClientDelegate* delegate)
    : config_(std::move(config)), delegate_(delegate) {}

AgentClient::~AgentClient() {
    cancel();
}

void AgentClient::setContext(Workbook* workbook, Sheet* sheet, LuauSandbox* sandbox) {
    workbook_ = workbook;
    sheet_ = sheet;
    sandbox_ = sandbox;
}

void AgentClient::clearContext() {
    workbook_ = nullptr;
    sheet_ = nullptr;
    sandbox_ = nullptr;
}

void AgentClient::sendMessage(const std::string& prompt, const std::string& conversationId) {
    if (request_) {
        // Already processing a request
        if (delegate_) {
            delegate_->onAgentError("Request already in progress");
        }
        return;
    }

    // Build request body
    std::string body = "{\"prompt\":\"";
    // Escape the prompt for JSON
    for (const char c : prompt) {
        switch (c) {
            case '"':
                body += "\\\"";
                break;
            case '\\':
                body += "\\\\";
                break;
            case '\b':
                body += "\\b";
                break;
            case '\f':
                body += "\\f";
                break;
            case '\n':
                body += "\\n";
                break;
            case '\r':
                body += "\\r";
                break;
            case '\t':
                body += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    body += buf;
                } else {
                    body += c;
                }
                break;
        }
    }
    body += "\"";

    // Add conversation ID if provided
    const std::string& convId = conversationId.empty() ? conversationId_ : conversationId;
    if (!convId.empty()) {
        body += ",\"conversation_id\":\"" + convId + "\"";
    }
    body += "}";

    // Parse server URL
    auto urlOpt = net::URL::parse(config_.serverUrl + "/api/agent/message");
    if (!urlOpt) {
        if (delegate_) {
            delegate_->onAgentError("Invalid server URL");
        }
        return;
    }

    // Create request
    request_ = net::HttpRequest::make(net::HttpMethod::POST, *urlOpt);
    if (!request_) {
        if (delegate_) {
            delegate_->onAgentError("Failed to create HTTP request");
        }
        return;
    }

    request_->setHeader("Content-Type", "application/json");
    request_->setBody(body);
    request_->setTimeout(120000);  // 2 minute timeout for streaming

    // Set up SSE parser
    sseParser_ = std::make_unique<net::SSEParser>(
        [this](const std::string& eventType, const std::string& data) {
            handleSSEEvent(eventType, data);
        });

    // Set stream callback
    request_->setStreamCallback([this](const uint8_t* data, size_t len) {
        if (sseParser_) {
            sseParser_->feed(data, len);
        }
    });

    // Set completion callback
    request_->setCallback([this](net::HttpRequest& /*req*/) {
        // Stream ended
        request_.reset();
        sseParser_.reset();
    });

    // Send request
    request_->sendAsyncStreaming();
}

void AgentClient::sendToolResult(const std::string& conversationId, const std::string& toolUseId,
                                 const std::string& result, bool isError) {
    if (request_) {
        // Already processing a request
        if (delegate_) {
            delegate_->onAgentError("Request already in progress");
        }
        return;
    }

    // Build request body
    std::string body = "{\"conversation_id\":\"" + conversationId + "\",";
    body += "\"tool_use_id\":\"" + toolUseId + "\",";
    body += "\"result\":\"";
    // Escape result for JSON
    for (const char c : result) {
        switch (c) {
            case '"':
                body += "\\\"";
                break;
            case '\\':
                body += "\\\\";
                break;
            case '\b':
                body += "\\b";
                break;
            case '\f':
                body += "\\f";
                break;
            case '\n':
                body += "\\n";
                break;
            case '\r':
                body += "\\r";
                break;
            case '\t':
                body += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    body += buf;
                } else {
                    body += c;
                }
                break;
        }
    }
    body += "\",";
    body += "\"is_error\":" + std::string(isError ? "true" : "false") + "}";

    // Parse server URL
    auto urlOpt = net::URL::parse(config_.serverUrl + "/api/agent/tool-result");
    if (!urlOpt) {
        if (delegate_) {
            delegate_->onAgentError("Invalid server URL");
        }
        return;
    }

    // Create request
    request_ = net::HttpRequest::make(net::HttpMethod::POST, *urlOpt);
    if (!request_) {
        if (delegate_) {
            delegate_->onAgentError("Failed to create HTTP request");
        }
        return;
    }

    request_->setHeader("Content-Type", "application/json");
    request_->setBody(body);
    request_->setTimeout(120000);

    // Set up SSE parser
    sseParser_ = std::make_unique<net::SSEParser>(
        [this](const std::string& eventType, const std::string& data) {
            handleSSEEvent(eventType, data);
        });

    // Set stream callback
    request_->setStreamCallback([this](const uint8_t* data, size_t len) {
        if (sseParser_) {
            sseParser_->feed(data, len);
        }
    });

    // Set completion callback
    request_->setCallback([this](net::HttpRequest& /*req*/) {
        // Stream ended
        request_.reset();
        sseParser_.reset();
    });

    // Send request
    request_->sendAsyncStreaming();
}

void AgentClient::cancel() {
    if (request_) {
        request_->cancel();
        request_.reset();
        sseParser_.reset();
    }
    pendingToolId_.clear();
    pendingToolName_.clear();
    pendingToolInput_.clear();
}

void AgentClient::clearConversation() {
    conversationId_.clear();
    pendingToolId_.clear();
    pendingToolName_.clear();
    pendingToolInput_.clear();
}

void AgentClient::handleSSEEvent(const std::string& eventType, const std::string& data) {
    if (eventType == "text") {
        // Parse text from JSON: {"text": "..."}
        const std::string text = parseJsonString(data, "text");
        if (delegate_) {
            delegate_->onAgentText(text);
        }
    } else if (eventType == "tool_use") {
        // Parse tool use from JSON: {"id": "...", "name": "...", "input": {...}}
        const std::string toolId = parseJsonString(data, "id");
        const std::string toolName = parseJsonString(data, "name");

        // Extract input object as raw JSON
        const size_t inputPos = data.find("\"input\":");
        std::string inputJson = "{}";
        if (inputPos != std::string::npos) {
            // Find the opening brace
            const size_t bracePos = data.find('{', inputPos);
            if (bracePos != std::string::npos) {
                // Find matching closing brace (simple approach, doesn't handle nested objects well)
                int depth = 1;
                size_t endPos = bracePos + 1;
                while (endPos < data.size() && depth > 0) {
                    if (data[endPos] == '{') {
                        ++depth;
                    } else if (data[endPos] == '}') {
                        --depth;
                    }
                    ++endPos;
                }
                inputJson = data.substr(bracePos, endPos - bracePos);
            }
        }

        // Store for potential auto-execution
        pendingToolId_ = toolId;
        pendingToolName_ = toolName;
        pendingToolInput_ = inputJson;

        if (delegate_) {
            delegate_->onAgentToolUse(toolId, toolName, inputJson);
        }
    } else if (eventType == "tool_result_needed") {
        const std::string toolUseId = parseJsonString(data, "tool_use_id");

        if (delegate_) {
            delegate_->onAgentToolResultNeeded(toolUseId);
        }

        // Auto-execute if enabled and we have context
        if (config_.autoExecuteTools && !pendingToolId_.empty()) {
            executeToolAndContinue(pendingToolId_, pendingToolName_, pendingToolInput_);
        }
    } else if (eventType == "done") {
        const std::string stopReason = parseJsonString(data, "stop_reason");
        const std::string convId = parseJsonString(data, "conversation_id");

        // Store conversation ID for future messages
        if (!convId.empty()) {
            conversationId_ = convId;
        }

        if (delegate_) {
            delegate_->onAgentComplete(stopReason, conversationId_);
        }

        // Clear pending tool state
        pendingToolId_.clear();
        pendingToolName_.clear();
        pendingToolInput_.clear();
    } else if (eventType == "error") {
        const std::string message = parseJsonString(data, "message");
        if (delegate_) {
            delegate_->onAgentError(message);
        }
    }
}

void AgentClient::executeToolAndContinue(const std::string& toolId, const std::string& toolName,
                                         const std::string& inputJson) {
    if (toolName != "execute_code") {
        // Unknown tool
        sendToolResult(conversationId_, toolId, "Unknown tool: " + toolName, true);
        return;
    }

    if (!sandbox_) {
        sendToolResult(conversationId_, toolId, "Luau sandbox not available", true);
        return;
    }

    // Parse the code from input JSON
    const std::string code = parseJsonString(inputJson, "code");
    if (code.empty()) {
        sendToolResult(conversationId_, toolId, "No code provided", true);
        return;
    }

    // Set context on sandbox
    if (workbook_ && sheet_) {
        sandbox_->setContext(workbook_, sheet_);
    }

    // Execute the code
    const ScriptResult result = sandbox_->execute(code);

    // Send result back
    if (result.success) {
        std::string output = result.output;
        if (output.empty()) {
            output = "(no output)";
        }
        sendToolResult(conversationId_, toolId, output, false);
    } else {
        sendToolResult(conversationId_, toolId, result.error, true);
    }
}

std::string AgentClient::parseJsonString(const std::string& json, const std::string& key) {
    // Simple JSON string parser - finds "key":"value" and returns value
    const std::string searchKey = "\"" + key + "\":\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        return "";
    }
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
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
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
                case 'u':
                    // Unicode escape - skip for now
                    pos += 4;
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

}  // namespace cells
