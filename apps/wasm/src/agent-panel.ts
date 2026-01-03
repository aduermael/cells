// Agent Panel - AI chat interface for the spreadsheet
// Manages the chat panel UI and communicates with the agent via CellsClient

import type { CellsClient } from "./client";
import type {
  AgentEventType,
  AgentToolUseData,
  AgentDoneData,
} from "./client-types";

// ============================================================================
// Types
// ============================================================================

interface AgentPanelElements {
  panel: HTMLElement;
  header: HTMLElement;
  title: HTMLElement;
  clearBtn: HTMLButtonElement;
  minimizeBtn: HTMLButtonElement;
  messages: HTMLElement;
  inputArea: HTMLElement;
  input: HTMLTextAreaElement;
  sendBtn: HTMLButtonElement;
  openBtn: HTMLButtonElement;
}

interface AgentPanelOptions {
  panel: HTMLElement;
  header: HTMLElement;
  title: HTMLElement;
  clearBtn: HTMLButtonElement;
  minimizeBtn: HTMLButtonElement;
  messages: HTMLElement;
  inputArea: HTMLElement;
  input: HTMLTextAreaElement;
  sendBtn: HTMLButtonElement;
  openBtn: HTMLButtonElement;
  getClient: () => CellsClient | null;
  getServerUrl: () => string;
  onViewportRefresh: () => void;
}

interface Message {
  role: "user" | "assistant";
  content: string;
  toolUses?: ToolUse[];
}

interface ToolUse {
  id: string;
  code: string;
  result?: string;
  isError?: boolean;
}

// ============================================================================
// AgentPanel Class
// ============================================================================

export class AgentPanel {
  private _elements: AgentPanelElements;
  private _getClient: () => CellsClient | null;
  private _getServerUrl: () => string;
  private _onViewportRefresh: () => void;

  private _messages: Message[] = [];
  private _conversationId: string = "";
  private _isProcessing: boolean = false;
  private _currentAssistantMessage: Message | null = null;
  private _currentMessageElement: HTMLElement | null = null;
  private _pendingToolUse: ToolUse | null = null;
  private _needsNewBubble: boolean = false; // Start new bubble after tool execution

  // Text animation queue for smooth streaming display
  // Adaptive algorithm: at least 1 char per 16ms, but scales up to finish buffer in ≤500ms
  private _textQueue: string = "";
  private _displayedText: string = "";
  private _animationFrameId: number | null = null;
  private _lastAnimationTime: number = 0;
  private static readonly MIN_FRAME_INTERVAL = 16; // Minimum 16ms between updates (~60fps)
  private static readonly MAX_DISPLAY_TIME = 500; // Max time to display entire buffer
  // Max frames available = 500/16 ≈ 31, so chars/frame = ceil(bufferSize/31)
  private static readonly MAX_FRAMES = Math.floor(
    AgentPanel.MAX_DISPLAY_TIME / AgentPanel.MIN_FRAME_INTERVAL
  );

  constructor(options: AgentPanelOptions) {
    this._elements = {
      panel: options.panel,
      header: options.header,
      title: options.title,
      clearBtn: options.clearBtn,
      minimizeBtn: options.minimizeBtn,
      messages: options.messages,
      inputArea: options.inputArea,
      input: options.input,
      sendBtn: options.sendBtn,
      openBtn: options.openBtn,
    };
    this._getClient = options.getClient;
    this._getServerUrl = options.getServerUrl;
    this._onViewportRefresh = options.onViewportRefresh;

    this._setupEventListeners();
    this._updateSendButton();
  }

  // ==========================================================================
  // Public Methods
  // ==========================================================================

  /** Initialize agent when client is ready */
  async initAgent(): Promise<void> {
    const client = this._getClient();
    if (!client) return;

    const serverUrl = this._getServerUrl();
    if (serverUrl == null) {
      console.warn("No agent server URL configured");
      return;
    }

    try {
      await client.initAgent(serverUrl);
      client.setOnAgentEvent((eventType, data) => this._handleAgentEvent(eventType, data));
    } catch (e) {
      console.error("Failed to initialize agent:", e);
    }
  }

  /** Show the chat panel */
  show(): void {
    this._elements.panel.classList.remove("hidden");
    this._elements.panel.classList.remove("minimized");
    this._elements.openBtn.classList.add("hidden");
    this._elements.input.focus();
  }

  /** Hide the chat panel */
  hide(): void {
    this._elements.panel.classList.add("hidden");
    this._elements.openBtn.classList.remove("hidden");
  }

  /** Minimize the chat panel (show only input) */
  minimize(): void {
    this._elements.panel.classList.add("minimized");
  }

  /** Expand the chat panel (show messages) */
  expand(): void {
    this._elements.panel.classList.remove("minimized");
  }

  /** Clear the conversation */
  async clear(): Promise<void> {
    const client = this._getClient();
    if (client) {
      await client.clearAgentConversation();
    }

    this._messages = [];
    this._conversationId = "";
    this._currentAssistantMessage = null;
    this._currentMessageElement = null;
    this._pendingToolUse = null;
    this._needsNewBubble = false;
    this._elements.messages.innerHTML = "";
  }

  // ==========================================================================
  // Private Methods - Event Handlers
  // ==========================================================================

  private _setupEventListeners(): void {
    // Open button
    this._elements.openBtn.addEventListener("click", () => this.show());

    // Minimize button
    this._elements.minimizeBtn.addEventListener("click", () => this.hide());

    // Clear button
    this._elements.clearBtn.addEventListener("click", () => this.clear());

    // Send button
    this._elements.sendBtn.addEventListener("click", () => this._send());

    // Input textarea
    this._elements.input.addEventListener("input", () => {
      this._autoResizeInput();
      this._updateSendButton();
    });

    // Enter to send (Shift+Enter for newline)
    this._elements.input.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.shiftKey) {
        e.preventDefault();
        this._send();
      }
    });
  }

  private _autoResizeInput(): void {
    const input = this._elements.input;
    input.style.height = "auto";
    const maxHeight = 120;
    input.style.height = Math.min(input.scrollHeight, maxHeight) + "px";
  }

  private _updateSendButton(): void {
    const hasText = this._elements.input.value.trim().length > 0;
    this._elements.sendBtn.disabled = !hasText || this._isProcessing;
  }

  // ==========================================================================
  // Private Methods - Sending Messages
  // ==========================================================================

  private async _send(): Promise<void> {
    const prompt = this._elements.input.value.trim();
    if (!prompt || this._isProcessing) return;

    const client = this._getClient();
    if (!client) {
      this._showError("Spreadsheet not loaded");
      return;
    }

    // Check if agent is initialized
    const initialized = await client.isAgentInitialized();
    if (!initialized) {
      await this.initAgent();
      const initCheck = await client.isAgentInitialized();
      if (!initCheck) {
        this._showError("AI agent not available. Please start the server.");
        return;
      }
    }

    // Add user message to UI
    this._addUserMessage(prompt);

    // Clear input
    this._elements.input.value = "";
    this._autoResizeInput();
    this._updateSendButton();

    // Start processing
    this._setProcessing(true);

    try {
      // Start assistant message element
      this._startAssistantMessage();

      // Send to agent
      await client.sendAgentMessage(prompt, this._conversationId);
    } catch (e) {
      this._setProcessing(false);
      this._showError(e instanceof Error ? e.message : "Failed to send message");
    }
  }

  // ==========================================================================
  // Private Methods - Agent Event Handling
  // ==========================================================================

  private _handleAgentEvent(eventType: AgentEventType, data: string): void {
    switch (eventType) {
      case "text":
        this._handleTextEvent(data);
        break;
      case "tool_use":
        this._handleToolUseEvent(data);
        break;
      case "tool_result_needed":
        this._handleToolResultNeeded(data);
        break;
      case "done":
        this._handleDoneEvent(data);
        break;
      case "error":
        this._handleErrorEvent(data);
        break;
    }
  }

  private _handleTextEvent(text: string): void {
    // After tool execution, start a new bubble for the follow-up text
    if (this._needsNewBubble) {
      this._finishCurrentBubble();
      this._needsNewBubble = false;
    }

    if (!this._currentAssistantMessage) {
      this._startAssistantMessage();
    }

    // Add to the full content (for data model)
    this._currentAssistantMessage!.content += text;

    // Add to animation queue (for smooth display)
    this._textQueue += text;
    this._startTextAnimation();
  }

  /** Finish current bubble without ending processing state */
  private _finishCurrentBubble(): void {
    this._flushTextAnimation();
    if (this._currentMessageElement) {
      this._currentMessageElement.classList.remove("streaming");
    }
    this._currentAssistantMessage = null;
    this._currentMessageElement = null;
  }

  /** Start the text animation loop if not already running */
  private _startTextAnimation(): void {
    if (this._animationFrameId !== null) return;

    const animate = (timestamp: number) => {
      // Throttle to MIN_FRAME_INTERVAL (at least 16ms between updates)
      if (timestamp - this._lastAnimationTime < AgentPanel.MIN_FRAME_INTERVAL) {
        this._animationFrameId = requestAnimationFrame(animate);
        return;
      }
      this._lastAnimationTime = timestamp;

      if (this._textQueue.length === 0) {
        // Nothing more to animate
        this._animationFrameId = null;
        return;
      }

      // Adaptive chars per frame: scale based on buffer size
      // - Small buffer (<31 chars): 1 char/frame for smooth display
      // - Large buffer: increase rate to finish within 500ms
      // Formula: ceil(bufferSize / maxFrames) ensures we finish in time
      const charsToShow = Math.max(1, Math.ceil(this._textQueue.length / AgentPanel.MAX_FRAMES));
      this._displayedText += this._textQueue.slice(0, charsToShow);
      this._textQueue = this._textQueue.slice(charsToShow);

      // Update the displayed content
      this._updateDisplayedContent();

      // Continue animation
      this._animationFrameId = requestAnimationFrame(animate);
    };

    this._animationFrameId = requestAnimationFrame(animate);
  }

  /** Update the message element with the currently displayed text */
  private _updateDisplayedContent(): void {
    if (!this._currentMessageElement) return;

    const contentEl = this._currentMessageElement.querySelector(".chat-message-content");
    if (contentEl) {
      contentEl.textContent = this._displayedText;
    }
    this._scrollToBottom();
  }

  /** Flush remaining text immediately (for when message completes) */
  private _flushTextAnimation(): void {
    if (this._animationFrameId !== null) {
      cancelAnimationFrame(this._animationFrameId);
      this._animationFrameId = null;
    }

    // Show all remaining text immediately
    this._displayedText += this._textQueue;
    this._textQueue = "";
    this._updateDisplayedContent();
  }

  private _handleToolUseEvent(dataJson: string): void {
    try {
      const data = JSON.parse(dataJson) as AgentToolUseData;

      if (!this._currentAssistantMessage) {
        this._startAssistantMessage();
      }

      // Flush any pending text before showing tool use
      this._flushTextAnimation();

      const toolUse: ToolUse = {
        id: data.id,
        code: data.input.code,
      };

      this._currentAssistantMessage!.toolUses = this._currentAssistantMessage!.toolUses || [];
      this._currentAssistantMessage!.toolUses.push(toolUse);
      this._pendingToolUse = toolUse;

      this._addToolUseElement(toolUse);
    } catch (e) {
      console.error("Failed to parse tool_use event:", e);
    }
  }

  private _handleToolResultNeeded(toolUseId: string): void {
    // The C++ AgentClient handles tool execution automatically via LuauSandbox
    // This event is informational - the result will come back and continue the conversation
    if (this._pendingToolUse && this._pendingToolUse.id === toolUseId) {
      // Tool is being executed, we'll get the result or continuation soon
      // Refresh viewport in case the script modified cells
      this._onViewportRefresh();

      // Next text after tool result should be a new bubble (new assistant turn)
      this._needsNewBubble = true;
    }
  }

  private _handleDoneEvent(dataJson: string): void {
    try {
      const data = JSON.parse(dataJson) as AgentDoneData;
      this._conversationId = data.conversation_id;
    } catch (e) {
      console.error("Failed to parse done event:", e);
    }

    this._finishAssistantMessage();
    this._setProcessing(false);

    // Refresh viewport after conversation turn completes
    this._onViewportRefresh();
  }

  private _handleErrorEvent(message: string): void {
    this._finishAssistantMessage();
    this._setProcessing(false);
    this._showError(message);
  }

  // ==========================================================================
  // Private Methods - UI Updates
  // ==========================================================================

  private _setProcessing(processing: boolean): void {
    this._isProcessing = processing;
    this._elements.panel.classList.toggle("loading", processing);
    this._updateSendButton();
  }

  private _addUserMessage(content: string): void {
    const message: Message = { role: "user", content };
    this._messages.push(message);

    const el = document.createElement("div");
    el.className = "chat-message user";
    el.innerHTML = `<div class="chat-message-content">${this._escapeHtml(content)}</div>`;
    this._elements.messages.appendChild(el);
    this._scrollToBottom();
  }

  private _startAssistantMessage(): void {
    this._currentAssistantMessage = { role: "assistant", content: "", toolUses: [] };
    this._messages.push(this._currentAssistantMessage);

    // Reset animation state for new message
    this._textQueue = "";
    this._displayedText = "";
    if (this._animationFrameId !== null) {
      cancelAnimationFrame(this._animationFrameId);
      this._animationFrameId = null;
    }

    const el = document.createElement("div");
    el.className = "chat-message assistant streaming";
    el.innerHTML = `<div class="chat-message-content"></div>`;
    this._currentMessageElement = el;
    this._elements.messages.appendChild(el);
    this._scrollToBottom();
  }

  private _addToolUseElement(toolUse: ToolUse): void {
    if (!this._currentMessageElement) return;

    const toolEl = document.createElement("div");
    toolEl.className = "chat-tool-use";
    toolEl.dataset.toolId = toolUse.id;
    toolEl.innerHTML = `
      <div class="chat-tool-header">
        <span class="chat-tool-icon">&#9660;</span>
        <span>Executed code</span>
      </div>
      <pre class="chat-tool-code">${this._escapeHtml(toolUse.code)}</pre>
    `;

    // Add collapse toggle
    const header = toolEl.querySelector(".chat-tool-header");
    header?.addEventListener("click", () => {
      toolEl.classList.toggle("collapsed");
    });

    this._currentMessageElement.appendChild(toolEl);
    this._scrollToBottom();
  }

  private _finishAssistantMessage(): void {
    // Flush any remaining text in the queue
    this._flushTextAnimation();

    if (this._currentMessageElement) {
      this._currentMessageElement.classList.remove("streaming");
    }
    this._currentAssistantMessage = null;
    this._currentMessageElement = null;
    this._pendingToolUse = null;
    this._needsNewBubble = false;
  }

  private _showError(message: string): void {
    const errorEl = document.createElement("div");
    errorEl.className = "chat-error";
    errorEl.textContent = message;
    this._elements.messages.appendChild(errorEl);
    this._scrollToBottom();
  }

  private _scrollToBottom(): void {
    this._elements.messages.scrollTop = this._elements.messages.scrollHeight;
  }

  private _escapeHtml(text: string): string {
    const div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
  }
}
