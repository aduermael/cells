// Agent handler for AI assistant interactions.
// Manages conversations and streams responses via SSE.

package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"
)

// Message represents a single message in a conversation.
type Message struct {
	Role       string      `json:"role"`        // "user", "assistant", or "tool_result"
	Content    string      `json:"content"`     // Text content for user/assistant messages
	ToolUseID  string      `json:"tool_use_id"` // ID for tool results
	ToolResult string      `json:"tool_result"` // Result from tool execution
	IsError    bool        `json:"is_error"`    // Whether tool result is an error
	ToolCalls  []ToolCall  `json:"tool_calls"`  // Tool calls made by assistant
}

// ToolCall represents a tool invocation by the assistant.
type ToolCall struct {
	ID    string                 `json:"id"`
	Name  string                 `json:"name"`
	Input map[string]interface{} `json:"input"`
}

// Conversation represents a conversation with the AI agent.
type Conversation struct {
	ID        string
	Messages  []Message
	CreatedAt time.Time
	UpdatedAt time.Time
	mu        sync.Mutex
}

// NewConversation creates a new conversation with a generated ID.
func NewConversation() *Conversation {
	return &Conversation{
		ID:        generateConversationID(),
		Messages:  make([]Message, 0),
		CreatedAt: time.Now(),
		UpdatedAt: time.Now(),
	}
}

// AddMessage appends a message to the conversation.
func (c *Conversation) AddMessage(msg Message) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.Messages = append(c.Messages, msg)
	c.UpdatedAt = time.Now()
}

// GetMessages returns a copy of all messages.
func (c *Conversation) GetMessages() []Message {
	c.mu.Lock()
	defer c.mu.Unlock()
	msgs := make([]Message, len(c.Messages))
	copy(msgs, c.Messages)
	return msgs
}

// ConversationStore manages active conversations.
type ConversationStore struct {
	conversations map[string]*Conversation
	mu            sync.RWMutex
	maxMessages   int           // Maximum messages per conversation
	timeout       time.Duration // Conversation expiry timeout
}

// NewConversationStore creates a new conversation store.
func NewConversationStore(maxMessages int, timeout time.Duration) *ConversationStore {
	return &ConversationStore{
		conversations: make(map[string]*Conversation),
		maxMessages:   maxMessages,
		timeout:       timeout,
	}
}

// Get retrieves a conversation by ID.
func (s *ConversationStore) Get(id string) *Conversation {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.conversations[id]
}

// GetOrCreate retrieves an existing conversation or creates a new one.
func (s *ConversationStore) GetOrCreate(id string) *Conversation {
	s.mu.Lock()
	defer s.mu.Unlock()

	if conv, ok := s.conversations[id]; ok {
		return conv
	}

	conv := NewConversation()
	if id != "" {
		conv.ID = id
	}
	s.conversations[conv.ID] = conv
	return conv
}

// Create creates a new conversation and stores it.
func (s *ConversationStore) Create() *Conversation {
	s.mu.Lock()
	defer s.mu.Unlock()

	conv := NewConversation()
	s.conversations[conv.ID] = conv
	return conv
}

// Delete removes a conversation.
func (s *ConversationStore) Delete(id string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.conversations, id)
}

// CleanupExpired removes conversations that have been inactive for too long.
func (s *ConversationStore) CleanupExpired() int {
	s.mu.Lock()
	defer s.mu.Unlock()

	removed := 0
	now := time.Now()

	for id, conv := range s.conversations {
		conv.mu.Lock()
		if now.Sub(conv.UpdatedAt) > s.timeout {
			delete(s.conversations, id)
			removed++
		}
		conv.mu.Unlock()
	}

	return removed
}

// TruncateOldMessages removes oldest messages if conversation exceeds max.
// Returns true if messages were truncated.
func (s *ConversationStore) TruncateOldMessages(conv *Conversation) bool {
	conv.mu.Lock()
	defer conv.mu.Unlock()

	if len(conv.Messages) <= s.maxMessages {
		return false
	}

	// Keep the most recent messages
	excess := len(conv.Messages) - s.maxMessages
	conv.Messages = conv.Messages[excess:]
	return true
}

// StartCleanupRoutine starts periodic cleanup of expired conversations.
func (s *ConversationStore) StartCleanupRoutine(interval time.Duration, stop <-chan struct{}) {
	go func() {
		ticker := time.NewTicker(interval)
		defer ticker.Stop()

		for {
			select {
			case <-ticker.C:
				s.CleanupExpired()
			case <-stop:
				return
			}
		}
	}()
}

// generateConversationID creates a random conversation ID.
func generateConversationID() string {
	bytes := make([]byte, 16)
	rand.Read(bytes)
	return "conv_" + hex.EncodeToString(bytes)
}

// AgentHandler handles agent API requests.
type AgentHandler struct {
	store    *ConversationStore
	client   *AnthropicClient
}

// NewAgentHandler creates a new agent handler.
func NewAgentHandler(store *ConversationStore) (*AgentHandler, error) {
	client, err := NewAnthropicClient()
	if err != nil {
		return nil, err
	}
	return &AgentHandler{
		store:  store,
		client: client,
	}, nil
}

// MessageRequest represents a request to send a message.
type MessageRequest struct {
	Prompt         string `json:"prompt"`
	ConversationID string `json:"conversation_id,omitempty"`
}

// ToolResultRequest represents a request to submit a tool result.
type ToolResultRequest struct {
	ConversationID string `json:"conversation_id"`
	ToolUseID      string `json:"tool_use_id"`
	Result         string `json:"result"`
	IsError        bool   `json:"is_error"`
}

// SSEWriter wraps http.ResponseWriter for SSE streaming.
type SSEWriter struct {
	w       http.ResponseWriter
	flusher http.Flusher
}

// NewSSEWriter creates a new SSE writer.
func NewSSEWriter(w http.ResponseWriter) (*SSEWriter, bool) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		return nil, false
	}

	// Set SSE headers
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

	return &SSEWriter{w: w, flusher: flusher}, true
}

// WriteEvent writes an SSE event.
func (s *SSEWriter) WriteEvent(event string, data interface{}) error {
	jsonData, err := json.Marshal(data)
	if err != nil {
		return err
	}
	fmt.Fprintf(s.w, "event: %s\ndata: %s\n\n", event, string(jsonData))
	s.flusher.Flush()
	return nil
}

// SSEHandler implements StreamEventHandler for writing to SSE.
type SSEHandler struct {
	writer   *SSEWriter
	convID   string
	textBuf  string
	toolUses []ToolCall
}

func (h *SSEHandler) OnText(text string) {
	h.textBuf += text
	h.writer.WriteEvent("text", map[string]string{"text": text})
}

func (h *SSEHandler) OnToolUse(id, name string, input json.RawMessage) {
	var inputMap map[string]interface{}
	json.Unmarshal(input, &inputMap)

	h.toolUses = append(h.toolUses, ToolCall{
		ID:    id,
		Name:  name,
		Input: inputMap,
	})

	h.writer.WriteEvent("tool_use", map[string]interface{}{
		"id":    id,
		"name":  name,
		"input": inputMap,
	})

	// Signal that tool result is needed
	h.writer.WriteEvent("tool_result_needed", map[string]string{
		"tool_use_id": id,
	})
}

func (h *SSEHandler) OnComplete(stopReason string) {
	h.writer.WriteEvent("done", map[string]string{
		"stop_reason":     stopReason,
		"conversation_id": h.convID,
	})
}

func (h *SSEHandler) OnError(message string) {
	h.writer.WriteEvent("error", map[string]string{"message": message})
}

// HandleMessage handles POST /api/agent/message
func (h *AgentHandler) HandleMessage(w http.ResponseWriter, r *http.Request) {
	if r.Method == "OPTIONS" {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		w.WriteHeader(http.StatusOK)
		return
	}

	if r.Method != "POST" {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req MessageRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	if req.Prompt == "" {
		http.Error(w, "Prompt is required", http.StatusBadRequest)
		return
	}

	// Get or create conversation
	var conv *Conversation
	if req.ConversationID != "" {
		conv = h.store.Get(req.ConversationID)
		if conv == nil {
			http.Error(w, "Conversation not found", http.StatusNotFound)
			return
		}
	} else {
		conv = h.store.Create()
	}

	// Add user message
	conv.AddMessage(Message{
		Role:    "user",
		Content: req.Prompt,
	})

	// Check for truncation
	if h.store.TruncateOldMessages(conv) {
		log.Printf("[AGENT] Truncated old messages in conversation %s", conv.ID)
	}

	// Set up SSE streaming
	sseWriter, ok := NewSSEWriter(w)
	if !ok {
		http.Error(w, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	// Create handler for streaming events
	sseHandler := &SSEHandler{
		writer: sseWriter,
		convID: conv.ID,
	}

	// Stream response from Anthropic
	log.Printf("[AGENT] Starting message stream for conversation %s", conv.ID)
	if err := h.client.StreamMessages(conv.GetMessages(), sseHandler); err != nil {
		log.Printf("[AGENT] Error streaming messages: %v", err)
		sseHandler.OnError(err.Error())
		return
	}

	// Save assistant message with any text and tool calls
	if sseHandler.textBuf != "" || len(sseHandler.toolUses) > 0 {
		conv.AddMessage(Message{
			Role:      "assistant",
			Content:   sseHandler.textBuf,
			ToolCalls: sseHandler.toolUses,
		})
	}

	log.Printf("[AGENT] Message stream complete for conversation %s", conv.ID)
}

// HandleToolResult handles POST /api/agent/tool-result
func (h *AgentHandler) HandleToolResult(w http.ResponseWriter, r *http.Request) {
	if r.Method == "OPTIONS" {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		w.WriteHeader(http.StatusOK)
		return
	}

	if r.Method != "POST" {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req ToolResultRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	if req.ConversationID == "" || req.ToolUseID == "" {
		http.Error(w, "conversation_id and tool_use_id are required", http.StatusBadRequest)
		return
	}

	// Get conversation
	conv := h.store.Get(req.ConversationID)
	if conv == nil {
		http.Error(w, "Conversation not found", http.StatusNotFound)
		return
	}

	// Add tool result message
	conv.AddMessage(Message{
		Role:       "tool_result",
		ToolUseID:  req.ToolUseID,
		ToolResult: req.Result,
		IsError:    req.IsError,
	})

	// Set up SSE streaming
	sseWriter, ok := NewSSEWriter(w)
	if !ok {
		http.Error(w, "Streaming not supported", http.StatusInternalServerError)
		return
	}

	// Create handler for streaming events
	sseHandler := &SSEHandler{
		writer: sseWriter,
		convID: conv.ID,
	}

	// Continue streaming from Anthropic
	log.Printf("[AGENT] Continuing conversation %s after tool result", conv.ID)
	if err := h.client.StreamMessages(conv.GetMessages(), sseHandler); err != nil {
		log.Printf("[AGENT] Error streaming messages: %v", err)
		sseHandler.OnError(err.Error())
		return
	}

	// Save assistant message with any text and tool calls
	if sseHandler.textBuf != "" || len(sseHandler.toolUses) > 0 {
		conv.AddMessage(Message{
			Role:      "assistant",
			Content:   sseHandler.textBuf,
			ToolCalls: sseHandler.toolUses,
		})
	}

	log.Printf("[AGENT] Tool result stream complete for conversation %s", conv.ID)
}

// HandleClearConversation handles POST /api/agent/clear
func (h *AgentHandler) HandleClearConversation(w http.ResponseWriter, r *http.Request) {
	if r.Method == "OPTIONS" {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		w.WriteHeader(http.StatusOK)
		return
	}

	if r.Method != "POST" {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		ConversationID string `json:"conversation_id"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "Invalid request body", http.StatusBadRequest)
		return
	}

	if req.ConversationID != "" {
		h.store.Delete(req.ConversationID)
	}

	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]bool{"success": true})
}
