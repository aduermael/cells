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
// This function ensures we don't break tool_use/tool_result pairing.
func (s *ConversationStore) TruncateOldMessages(conv *Conversation) bool {
	conv.mu.Lock()
	defer conv.mu.Unlock()

	if len(conv.Messages) <= s.maxMessages {
		return false
	}

	// Keep the most recent messages, but find a safe cut point
	excess := len(conv.Messages) - s.maxMessages

	// Find a safe cut point - never cut between an assistant message with tool_calls
	// and its corresponding tool_result
	for excess < len(conv.Messages) {
		msgAtCut := conv.Messages[excess]

		// If we're about to keep a tool_result, make sure its tool_use is also kept
		if msgAtCut.Role == "tool_result" {
			excess--
			continue
		}

		// If we're about to discard an assistant message with tool_calls,
		// make sure we also discard the following tool_results
		if excess > 0 {
			prevMsg := conv.Messages[excess-1]
			if prevMsg.Role == "assistant" && len(prevMsg.ToolCalls) > 0 {
				// Check if there are tool_results after it that we'd be keeping
				// If so, we need to include this assistant message too
				for i := excess; i < len(conv.Messages); i++ {
					if conv.Messages[i].Role == "tool_result" {
						// Found a tool_result we'd be keeping - need to keep the assistant too
						excess--
						break
					}
					if conv.Messages[i].Role == "assistant" || conv.Messages[i].Role == "user" {
						// Hit another message boundary, no orphaned tool_results
						break
					}
				}
				continue
			}
		}

		break
	}

	if excess <= 0 {
		return false
	}

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

// getPendingToolUses returns tool_use IDs that don't have matching tool_results yet.
// This checks if the last assistant message has tool calls without corresponding results.
func getPendingToolUses(messages []Message) []string {
	if len(messages) == 0 {
		return nil
	}

	// Find the last assistant message
	var lastAssistantIdx int = -1
	for i := len(messages) - 1; i >= 0; i-- {
		if messages[i].Role == "assistant" {
			lastAssistantIdx = i
			break
		}
	}

	if lastAssistantIdx < 0 {
		return nil
	}

	lastAssistant := messages[lastAssistantIdx]
	if len(lastAssistant.ToolCalls) == 0 {
		return nil
	}

	// Collect all tool_use IDs from the last assistant message
	pendingIDs := make(map[string]bool)
	for _, tc := range lastAssistant.ToolCalls {
		pendingIDs[tc.ID] = true
	}

	// Remove IDs that have tool_results after the assistant message
	for i := lastAssistantIdx + 1; i < len(messages); i++ {
		if messages[i].Role == "tool_result" {
			delete(pendingIDs, messages[i].ToolUseID)
		}
	}

	// Convert remaining to slice
	var pending []string
	for id := range pendingIDs {
		pending = append(pending, id)
	}
	return pending
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
	conv     *Conversation // Reference to conversation for immediate saves
	textBuf  string
	toolUses []ToolCall
	saved    bool // Whether we've already saved the assistant message
}

func (h *SSEHandler) OnText(text string) {
	h.textBuf += text
	log.Printf("[AGENT] SSE text event: %q", text)
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

	// Signal that tool result is needed (include conversation_id for client to use)
	h.writer.WriteEvent("tool_result_needed", map[string]string{
		"tool_use_id":     id,
		"conversation_id": h.convID,
	})
}

func (h *SSEHandler) OnComplete(stopReason string) {
	// Save assistant message BEFORE sending done event to client
	// This prevents race condition where client calls tool-result before message is saved
	if !h.saved && (h.textBuf != "" || len(h.toolUses) > 0) {
		h.conv.AddMessage(Message{
			Role:      "assistant",
			Content:   h.textBuf,
			ToolCalls: h.toolUses,
		})
		h.saved = true
		log.Printf("[AGENT] Saved assistant message (text=%d chars, tools=%d)", len(h.textBuf), len(h.toolUses))
	}

	log.Printf("[AGENT] SSE done event: stop_reason=%q, conversation_id=%q", stopReason, h.convID)
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

		// Check for pending tool calls that need results first
		messages := conv.GetMessages()
		if pending := getPendingToolUses(messages); len(pending) > 0 {
			log.Printf("[AGENT] ERROR: Cannot add user message - %d pending tool_use(s) need tool_result first", len(pending))
			for _, id := range pending {
				log.Printf("[AGENT]   Pending tool_use_id: %s", id)
			}
			http.Error(w, fmt.Sprintf("Cannot send message: %d pending tool call(s) need results first", len(pending)), http.StatusConflict)
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
		conv:   conv,
	}

	// Stream response from Anthropic
	log.Printf("[AGENT] Starting message stream for conversation %s", conv.ID)
	if err := h.client.StreamMessages(conv.GetMessages(), sseHandler); err != nil {
		log.Printf("[AGENT] Error streaming messages: %v", err)
		sseHandler.OnError(err.Error())
		return
	}

	// Assistant message is saved in OnComplete to prevent race conditions
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

	// Validate: the tool_use_id must exist in the last assistant message
	messages := conv.GetMessages()
	if len(messages) == 0 {
		http.Error(w, "No messages in conversation", http.StatusBadRequest)
		return
	}

	// Find the last assistant message (skip any existing tool_results)
	var lastAssistant *Message
	for i := len(messages) - 1; i >= 0; i-- {
		if messages[i].Role == "assistant" {
			lastAssistant = &messages[i]
			break
		}
	}

	if lastAssistant == nil {
		http.Error(w, "No assistant message found for tool result", http.StatusBadRequest)
		return
	}

	// Check if the tool_use_id exists in that assistant message
	found := false
	for _, tc := range lastAssistant.ToolCalls {
		if tc.ID == req.ToolUseID {
			found = true
			break
		}
	}
	if !found {
		log.Printf("[AGENT] ERROR: tool_use_id %s not found in last assistant message (has %d tool calls)", req.ToolUseID, len(lastAssistant.ToolCalls))
		for i, tc := range lastAssistant.ToolCalls {
			log.Printf("[AGENT]   ToolCall[%d]: id=%s name=%s", i, tc.ID, tc.Name)
		}
		http.Error(w, fmt.Sprintf("tool_use_id %s not found in assistant message", req.ToolUseID), http.StatusBadRequest)
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
		conv:   conv,
	}

	// Continue streaming from Anthropic
	log.Printf("[AGENT] Continuing conversation %s after tool result", conv.ID)
	if err := h.client.StreamMessages(conv.GetMessages(), sseHandler); err != nil {
		log.Printf("[AGENT] Error streaming messages: %v", err)
		sseHandler.OnError(err.Error())
		return
	}

	// Assistant message is saved in OnComplete to prevent race conditions
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
