// Anthropic API client with SSE streaming support.

package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
)

// truncateForLog truncates a string for logging purposes.
func truncateForLog(s string, maxLen int) string {
	if len(s) <= maxLen {
		return s
	}
	return s[:maxLen] + "..."
}

const (
	anthropicAPIURL     = "https://api.anthropic.com/v1/messages"
	anthropicAPIVersion = "2023-06-01"
	defaultModel        = "claude-sonnet-4-20250514"
	maxTokens           = 4096
)

// AnthropicClient handles communication with the Anthropic API.
type AnthropicClient struct {
	apiKey     string
	httpClient *http.Client
}

// NewAnthropicClient creates a new Anthropic API client.
func NewAnthropicClient() (*AnthropicClient, error) {
	apiKey := os.Getenv("ANTHROPIC_API_KEY")
	if apiKey == "" {
		return nil, fmt.Errorf("ANTHROPIC_API_KEY environment variable not set")
	}
	return &AnthropicClient{
		apiKey:     apiKey,
		httpClient: &http.Client{},
	}, nil
}

// Tool represents an Anthropic tool definition.
type Tool struct {
	Name        string          `json:"name"`
	Description string          `json:"description"`
	InputSchema json.RawMessage `json:"input_schema"`
}

// APIMessage represents a message in the Anthropic API format.
type APIMessage struct {
	Role    string      `json:"role"`
	Content interface{} `json:"content"` // string or []ContentBlock
}

// ContentBlock represents a content block in a message.
type ContentBlock struct {
	Type      string          `json:"type"`
	Text      string          `json:"text,omitempty"`
	ID        string          `json:"id,omitempty"`
	Name      string          `json:"name,omitempty"`
	Input     json.RawMessage `json:"input,omitempty"`
	ToolUseID string          `json:"tool_use_id,omitempty"`
	Content   string          `json:"content,omitempty"`
	IsError   bool            `json:"is_error,omitempty"`
}

// MessagesRequest represents a request to the Anthropic Messages API.
type MessagesRequest struct {
	Model     string       `json:"model"`
	MaxTokens int          `json:"max_tokens"`
	System    string       `json:"system,omitempty"`
	Messages  []APIMessage `json:"messages"`
	Tools     []Tool       `json:"tools,omitempty"`
	Stream    bool         `json:"stream"`
}

// SSEEvent represents a parsed SSE event.
type SSEEvent struct {
	Event string
	Data  string
}

// StreamEventHandler handles streaming events from the API.
type StreamEventHandler interface {
	OnText(text string)
	OnToolUse(id, name string, input json.RawMessage)
	OnComplete(stopReason string)
	OnError(message string)
}

// GetExecuteCodeTool returns the execute_code tool definition.
func GetExecuteCodeTool() Tool {
	return Tool{
		Name:        "execute_code",
		Description: "Execute Luau code to interact with the spreadsheet. API: getCell(ref, {create?}):Cell?, setCell(ref, value), getSheet({name?|index?}|name|index):Sheet?, selectSheet(sheet|name|index), addSheet(name?):Sheet, selectRange(range), deleteRange(range), fillRange({from, to}), setColumnWidth(col, {width}), setRowHeight(row, {height}), moveColumn(col, {to}), setDocumentTitle(title), getDocumentTitle():string, setFormat(range, formatId), setStyle(range, styleTable), getFormats():table. Cell has .value, .formula, .ref, .format, .style, .dependents, .dependencies. Sheet has .name. Style table: {bold, italic, underline, bgColor, textColor, fontFamily, fontSize, hAlign, vAlign}. Use print() for output.",
		InputSchema: json.RawMessage(`{
			"type": "object",
			"properties": {
				"code": {
					"type": "string",
					"description": "Luau code to execute"
				}
			},
			"required": ["code"]
		}`),
	}
}

// GetSystemPrompt returns the system prompt for the agent.
func GetSystemPrompt() string {
	return `You are an AI assistant that helps users interact with spreadsheets. You can execute Luau code to read and modify the spreadsheet.

Available API:
- getCell(ref, {create?}): Get a cell by reference (e.g., "A1", "Sheet1!B2"). Returns Cell object or nil. Pass {create=true} to create if missing.
- setCell(ref, value): Set a cell's value by reference.
- getSheet({name?, index?}): Get a sheet by name or 1-based index. Returns Sheet object or nil. No args = active sheet.
- selectSheet(sheet|name|index): Switch to a sheet by object, name, or index.
- addSheet(name?): Add a new sheet with optional name. Returns Sheet object.
- selectRange(range): Select a range (e.g., "A1:B5") in the active sheet.
- deleteRange(range): Delete all cells in the specified range.
- fillRange({from, to}): Copy formula/value from 'from' cell to 'to' range.
- setColumnWidth(col, {width}): Set column width in pixels.
- setRowHeight(row, {height}): Set row height in pixels.
- moveColumn(col, {to}): Move a column to a new position.
- setDocumentTitle(title): Set the document title.
- getDocumentTitle(): Get the current document title.
- setFormat(range, formatId): Apply number format to range (e.g., "A1:B10", "FMT_C002").
- setStyle(range, styleTable): Apply style to range. styleTable: {bold, italic, underline, bgColor, textColor, fontFamily, fontSize, hAlign, vAlign}.
- getFormats(): Returns table of available format IDs with descriptions.

Cell properties:
- .value (read/write): Cell value
- .formula (read-only): Formula string if cell has formula
- .ref (read-only): Cell reference string like "A1"
- .format (read/write): Format ID string (e.g., "FMT_C002") or nil to clear
- .style (read/write): Style table {bold, italic, underline, bgColor, textColor, fontFamily, fontSize, hAlign, vAlign} or nil to clear
- .dependents (read-only): Array of cells that depend on this cell
- .dependencies (read-only): Array of cells this cell depends on

Sheet properties: .name (read-only)

Style properties:
- bold, italic, underline: boolean
- bgColor, textColor: CSS hex color (e.g., "#FF0000") or nil
- fontFamily: Font name string (e.g., "Arial")
- fontSize: Number in points (e.g., 12)
- hAlign: Horizontal alignment - use ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT, or ALIGN_JUSTIFY
- vAlign: Vertical alignment - use VALIGN_TOP, VALIGN_MIDDLE, or VALIGN_BOTTOM

Color constants: COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE, COLOR_BLACK, COLOR_GRAY, COLOR_ORANGE

Common format IDs:
- FMT_C002: Currency with 2 decimals ($1,234.56)
- FMT_P002: Percentage with 2 decimals (12.34%)
- FMT_N002: Number with 2 decimals (1,234.56)
- Use getFormats() for full list

Use print() to output information to the user.

Examples:
- Read a cell: print(getCell("A1").value)
- Write a cell: setCell("B2", 42) or getCell("B2", {create=true}).value = 42
- Sum formula: setCell("C1", "=SUM(A1:B1)")
- Find dependents: for _, dep in getCell("A1").dependents do print(dep.ref) end
- Make header bold: setStyle("A1:F1", {bold=true})
- Blue background on header: setStyle("A1:F1", {bgColor=COLOR_BLUE, textColor=COLOR_WHITE})
- Center align a range: setStyle("B2:D10", {hAlign=ALIGN_CENTER})
- Format as currency: setFormat("B2:B100", "FMT_C002")
- Format as percentage: setFormat("C2:C100", "FMT_P002")
- Single cell style: getCell("A1").style = {bold=true, italic=true, bgColor="#FFFF00"}
- Clear style: getCell("A1").style = nil

Be concise and helpful. When you need to inspect or modify the spreadsheet, use the execute_code tool.`
}

// ConvertToAPIMessages converts internal messages to Anthropic API format.
// This handles batching multiple tool_results into a single user message,
// which is required by the Anthropic API.
func ConvertToAPIMessages(messages []Message) []APIMessage {
	var apiMessages []APIMessage
	var currentBlocks []ContentBlock
	var currentRole string

	flushBlocks := func() {
		if len(currentBlocks) > 0 {
			apiMessages = append(apiMessages, APIMessage{
				Role:    currentRole,
				Content: currentBlocks,
			})
			currentBlocks = nil
			currentRole = ""
		}
	}

	for _, msg := range messages {
		switch msg.Role {
		case "user":
			flushBlocks()
			apiMessages = append(apiMessages, APIMessage{
				Role:    "user",
				Content: msg.Content,
			})

		case "assistant":
			flushBlocks()
			currentRole = "assistant"
			if msg.Content != "" {
				currentBlocks = append(currentBlocks, ContentBlock{
					Type: "text",
					Text: msg.Content,
				})
			}
			for _, tc := range msg.ToolCalls {
				inputJSON, _ := json.Marshal(tc.Input)
				currentBlocks = append(currentBlocks, ContentBlock{
					Type:  "tool_use",
					ID:    tc.ID,
					Name:  tc.Name,
					Input: inputJSON,
				})
			}
			if len(currentBlocks) > 0 {
				flushBlocks()
			}

		case "tool_result":
			// Tool results must be in a user message
			// Multiple consecutive tool_results should be batched into one user message
			if currentRole != "user" {
				flushBlocks()
				currentRole = "user"
			}
			currentBlocks = append(currentBlocks, ContentBlock{
				Type:      "tool_result",
				ToolUseID: msg.ToolUseID,
				Content:   msg.ToolResult,
				IsError:   msg.IsError,
			})
			// Don't flush yet - there might be more tool_results to batch
		}
	}

	flushBlocks()
	return apiMessages
}

// StreamMessages sends a streaming request to the Anthropic API.
func (c *AnthropicClient) StreamMessages(messages []Message, handler StreamEventHandler) error {
	apiMessages := ConvertToAPIMessages(messages)

	// Debug: log the message structure before sending
	log.Printf("[ANTHROPIC] Sending %d API messages:", len(apiMessages))
	for i, msg := range apiMessages {
		switch content := msg.Content.(type) {
		case string:
			log.Printf("[ANTHROPIC]   [%d] role=%s content=%q", i, msg.Role, truncateForLog(content, 50))
		case []ContentBlock:
			log.Printf("[ANTHROPIC]   [%d] role=%s blocks=%d", i, msg.Role, len(content))
			for j, block := range content {
				switch block.Type {
				case "text":
					log.Printf("[ANTHROPIC]       [%d.%d] type=text text=%q", i, j, truncateForLog(block.Text, 50))
				case "tool_use":
					log.Printf("[ANTHROPIC]       [%d.%d] type=tool_use id=%s name=%s", i, j, block.ID, block.Name)
				case "tool_result":
					log.Printf("[ANTHROPIC]       [%d.%d] type=tool_result tool_use_id=%s", i, j, block.ToolUseID)
				}
			}
		}
	}

	reqBody := MessagesRequest{
		Model:     defaultModel,
		MaxTokens: maxTokens,
		System:    GetSystemPrompt(),
		Messages:  apiMessages,
		Tools:     []Tool{GetExecuteCodeTool()},
		Stream:    true,
	}

	bodyBytes, err := json.Marshal(reqBody)
	if err != nil {
		return fmt.Errorf("failed to marshal request: %w", err)
	}

	req, err := http.NewRequest("POST", anthropicAPIURL, bytes.NewReader(bodyBytes))
	if err != nil {
		return fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("x-api-key", c.apiKey)
	req.Header.Set("anthropic-version", anthropicAPIVersion)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("API error (status %d): %s", resp.StatusCode, string(body))
	}

	return c.parseSSEStream(resp.Body, handler)
}

// parseSSEStream parses the SSE stream from the API response.
func (c *AnthropicClient) parseSSEStream(reader io.Reader, handler StreamEventHandler) error {
	scanner := bufio.NewScanner(reader)
	var currentEvent SSEEvent

	// Track current tool use being built
	var currentToolID string
	var currentToolName string
	var currentToolInput strings.Builder

	for scanner.Scan() {
		line := scanner.Text()

		if strings.HasPrefix(line, "event: ") {
			currentEvent.Event = strings.TrimPrefix(line, "event: ")
		} else if strings.HasPrefix(line, "data: ") {
			currentEvent.Data = strings.TrimPrefix(line, "data: ")
		} else if line == "" && currentEvent.Event != "" {
			// Process completed event
			if err := c.handleEvent(currentEvent, handler, &currentToolID, &currentToolName, &currentToolInput); err != nil {
				return err
			}
			currentEvent = SSEEvent{}
		}
	}

	return scanner.Err()
}

// handleEvent processes a single SSE event.
func (c *AnthropicClient) handleEvent(
	event SSEEvent,
	handler StreamEventHandler,
	currentToolID *string,
	currentToolName *string,
	currentToolInput *strings.Builder,
) error {
	// Debug: log all events
	log.Printf("[ANTHROPIC] SSE event: %s data=%s", event.Event, truncateForLog(event.Data, 100))

	switch event.Event {
	case "content_block_start":
		var data struct {
			ContentBlock struct {
				Type string `json:"type"`
				ID   string `json:"id"`
				Name string `json:"name"`
			} `json:"content_block"`
		}
		if err := json.Unmarshal([]byte(event.Data), &data); err == nil {
			log.Printf("[ANTHROPIC] content_block_start: type=%s id=%s name=%s", data.ContentBlock.Type, data.ContentBlock.ID, data.ContentBlock.Name)
			if data.ContentBlock.Type == "tool_use" {
				*currentToolID = data.ContentBlock.ID
				*currentToolName = data.ContentBlock.Name
				currentToolInput.Reset()
				log.Printf("[ANTHROPIC] Started building tool_use: id=%s name=%s", *currentToolID, *currentToolName)
			}
		} else {
			log.Printf("[ANTHROPIC] ERROR parsing content_block_start: %v", err)
		}

	case "content_block_delta":
		var data struct {
			Delta struct {
				Type        string `json:"type"`
				Text        string `json:"text"`
				PartialJSON string `json:"partial_json"`
			} `json:"delta"`
		}
		if err := json.Unmarshal([]byte(event.Data), &data); err == nil {
			switch data.Delta.Type {
			case "text_delta":
				handler.OnText(data.Delta.Text)
			case "input_json_delta":
				currentToolInput.WriteString(data.Delta.PartialJSON)
			}
		}

	case "content_block_stop":
		// If we were building a tool use, emit it now
		log.Printf("[ANTHROPIC] content_block_stop: currentToolID=%q", *currentToolID)
		if *currentToolID != "" {
			inputJSON := currentToolInput.String()
			log.Printf("[ANTHROPIC] Emitting tool_use: id=%s name=%s input=%s", *currentToolID, *currentToolName, truncateForLog(inputJSON, 100))
			handler.OnToolUse(*currentToolID, *currentToolName, json.RawMessage(inputJSON))
			*currentToolID = ""
			*currentToolName = ""
			currentToolInput.Reset()
		}

	case "message_stop":
		// Message complete

	case "message_delta":
		var data struct {
			Delta struct {
				StopReason string `json:"stop_reason"`
			} `json:"delta"`
		}
		if err := json.Unmarshal([]byte(event.Data), &data); err == nil {
			if data.Delta.StopReason != "" {
				handler.OnComplete(data.Delta.StopReason)
			}
		}

	case "error":
		var data struct {
			Error struct {
				Message string `json:"message"`
			} `json:"error"`
		}
		if err := json.Unmarshal([]byte(event.Data), &data); err == nil {
			handler.OnError(data.Error.Message)
		}
	}

	return nil
}
