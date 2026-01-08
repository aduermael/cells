// =============================================================================
// AI Agent Client
// =============================================================================
//
// HTTP client for communicating with the AI agent server (Go backend).
// Uses Server-Sent Events (SSE) for streaming responses.
//
// Key responsibilities:
// - Send user prompts to the AI agent server
// - Stream assistant responses via SSE
// - Handle tool use requests (execute Luau code)
// - Manage conversation state across multiple turns
//
// Delegate callbacks:
// - onAgentText(): Streaming assistant text
// - onAgentToolUse(): Tool invocation request
// - onAgentComplete(): Message stream finished
// - onAgentError(): Error occurred
//
// Tool execution flow:
// 1. Server sends tool_use event with code
// 2. Client executes code in LuauSandbox
// 3. Client sends result back via sendToolResult()
// 4. Server continues with next response
//
// Dependencies: net/http_request.h, net/sse_parser.h, luau_sandbox.h
// Used by: bindings.cc (WASM AI integration)
//
// =============================================================================

#ifndef CELLS_AGENT_CLIENT_H_
#define CELLS_AGENT_CLIENT_H_

#include <cstdint>

#include <functional>
#include <memory>
#include <string>

namespace cells {

// Forward declarations
struct Workbook;
struct Sheet;
class LuauSandbox;

namespace net {
class HttpRequest;
class SSEParser;
}  // namespace net

// Delegate interface for agent events
// Implementations receive callbacks for streaming text, tool requests, etc.
class AgentClientDelegate {
public:
    virtual ~AgentClientDelegate() = default;

    // Called when streaming text is received from the assistant
    virtual void onAgentText(const std::string& text) = 0;

    // Called when a tool use is requested
    // toolId: unique ID for this tool invocation
    // name: tool name (e.g., "execute_code")
    // inputJson: JSON string of tool input
    virtual void onAgentToolUse(const std::string& toolId, const std::string& name,
                                const std::string& inputJson) = 0;

    // Called when tool result is needed (after onAgentToolUse)
    // This signals the client should execute the tool and call sendToolResult
    virtual void onAgentToolResultNeeded(const std::string& toolUseId) = 0;

    // Called when the message stream is complete
    // stopReason: "end_turn", "tool_use", etc.
    // conversationId: the conversation ID for continuing the conversation
    virtual void onAgentComplete(const std::string& stopReason,
                                 const std::string& conversationId) = 0;

    // Called when an error occurs
    virtual void onAgentError(const std::string& message) = 0;
};

// Agent client configuration
struct AgentClientConfig {
    std::string serverUrl = "http://localhost:8081";  // Server URL
    bool autoExecuteTools = true;                     // Automatically execute tools
};

// AgentClient - communicates with the AI agent server
//
// Sends user messages to the server, receives streaming responses via SSE,
// and optionally auto-executes tool calls (Luau code execution).
//
// Usage:
//   AgentClient client(config, delegate);
//   client.setContext(workbook, sheet, &sandbox);
//   client.sendMessage("What's in cell A1?");
//   // ... delegate receives callbacks ...
//
class AgentClient {
public:
    AgentClient(AgentClientConfig config, AgentClientDelegate* delegate);
    ~AgentClient();

    // Non-copyable
    AgentClient(const AgentClient&) = delete;
    AgentClient& operator=(const AgentClient&) = delete;

    // Set the workbook/sheet context for tool execution
    void setContext(Workbook* workbook, Sheet* sheet, LuauSandbox* sandbox);

    // Clear the context
    void clearContext();

    // Send a message to the agent
    // conversationId: optional, empty string to start a new conversation
    void sendMessage(const std::string& prompt, const std::string& conversationId = "");

    // Send a tool result back to the server
    // This continues the conversation after a tool execution
    void sendToolResult(const std::string& conversationId, const std::string& toolUseId,
                        const std::string& result, bool isError);

    // Cancel any in-progress request
    void cancel();

    // Get the current conversation ID (set after first response)
    [[nodiscard]] const std::string& getConversationId() const { return conversationId_; }

    // Clear the current conversation (start fresh)
    void clearConversation();

    // Check if a request is in progress
    [[nodiscard]] bool isProcessing() const { return request_ != nullptr; }

private:
    // Handle SSE events
    void handleSSEEvent(const std::string& eventType, const std::string& data);

    // Execute a tool and send the result back
    void executeToolAndContinue(const std::string& toolId, const std::string& toolName,
                                const std::string& inputJson);

    // Parse JSON helpers
    static std::string parseJsonString(const std::string& json, const std::string& key);

    AgentClientConfig config_;
    AgentClientDelegate* delegate_;

    // Request state
    std::unique_ptr<net::HttpRequest> request_;
    std::unique_ptr<net::SSEParser> sseParser_;
    std::string conversationId_;

    // Context for tool execution
    Workbook* workbook_{nullptr};
    Sheet* sheet_{nullptr};
    LuauSandbox* sandbox_{nullptr};

    // Pending tool execution (for auto-execute)
    std::string pendingToolId_;
    std::string pendingToolName_;
    std::string pendingToolInput_;
};

}  // namespace cells

#endif  // CELLS_AGENT_CLIENT_H_
