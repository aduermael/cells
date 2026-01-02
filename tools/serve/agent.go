// Agent handler for AI assistant interactions.
// Manages conversations and streams responses via SSE.

package main

import (
	"crypto/rand"
	"encoding/hex"
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
