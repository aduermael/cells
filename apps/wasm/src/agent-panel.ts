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
    if (!serverUrl) {
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
    if (!this._currentAssistantMessage) {
      this._startAssistantMessage();
    }

    this._currentAssistantMessage!.content += text;
    this._updateCurrentMessageContent();
  }

  private _handleToolUseEvent(dataJson: string): void {
    try {
      const data = JSON.parse(dataJson) as AgentToolUseData;

      if (!this._currentAssistantMessage) {
        this._startAssistantMessage();
      }

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

    const el = document.createElement("div");
    el.className = "chat-message assistant streaming";
    el.innerHTML = `<div class="chat-message-content"></div>`;
    this._currentMessageElement = el;
    this._elements.messages.appendChild(el);
    this._scrollToBottom();
  }

  private _updateCurrentMessageContent(): void {
    if (!this._currentMessageElement || !this._currentAssistantMessage) return;

    const contentEl = this._currentMessageElement.querySelector(".chat-message-content");
    if (contentEl) {
      contentEl.textContent = this._currentAssistantMessage.content;
    }
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
    if (this._currentMessageElement) {
      this._currentMessageElement.classList.remove("streaming");
    }
    this._currentAssistantMessage = null;
    this._currentMessageElement = null;
    this._pendingToolUse = null;
  }

  private _showError(message: string): void {
    const errorEl = document.createElement("div");
    errorEl.className = "chat-error";
    errorEl.textContent = message;
    this._elements.messages.appendChild(errorEl);
    this._scrollToBottom();

    // Auto-remove after 5 seconds
    setTimeout(() => errorEl.remove(), 5000);
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
