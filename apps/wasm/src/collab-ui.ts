// Collaboration UI Module
// Manages the UI elements for collaboration status, share button, and peer indicators

// Import CollabState from the new C++ adapter (with fallback to old collab-manager)
// The CppSyncAdapter exports CollabState for backwards compatibility
import { CollabState } from "./cpp-sync-adapter";
import { generateRandomName } from "./presence";
import type { SyncStateType, PeerPresence, RoomId, SyncStats } from "./types";

// ============================================================================
// Types
// ============================================================================

/** Collab manager interface (subset of CppSyncAdapter) */
interface CollabManager {
  state: SyncStateType;
  roomId: RoomId | null;
  debugMode: boolean;
  stats: SyncStats;
  on(event: string, callback: (...args: unknown[]) => void): void;
  off(event: string, callback: (...args: unknown[]) => void): void;
  getConnectedPeerCount(): number;
  getAverageLatency(): number | null;
  setDebugMode(enabled: boolean): void;
  forceReconnect(): Promise<void>;
  resetSyncState(): Promise<void>;
  downloadDebugData(): void;
}

/** Presence manager interface (subset of CppSyncAdapter) */
interface PresenceManager {
  localName: string | null;
  on(event: string, callback: (...args: unknown[]) => void): void;
  off(event: string, callback: (...args: unknown[]) => void): void;
  getRemotePeers(): Map<string, PeerPresence>;
  setLocalName(name: string): Promise<void>;
}

/** Room manager interface */
interface RoomManager {
  currentRoomId: RoomId | null;
  createAndJoinRoom(): Promise<void>;
}

/** Callback type for initialization requests */
type InitializeRequestCallback = () => Promise<void>;

/** Options for CollabUI constructor */
interface CollabUIOptions {
  container: HTMLElement;
  collabManager?: CollabManager;
  presenceManager?: PresenceManager;
  roomManager?: RoomManager;
}

// ============================================================================
// Constants
// ============================================================================

/**
 * Status text for each collaboration state
 */
const STATUS_TEXT: Record<string, string> = {
  [CollabState.OFFLINE]: "Offline",
  [CollabState.CONNECTING]: "Connecting...",
  [CollabState.SYNCING]: "Syncing...",
  [CollabState.ONLINE]: "Online",
};

/**
 * Detailed status text shown in the details panel
 */
const DETAILED_STATUS_TEXT: Record<string, string> = {
  [CollabState.OFFLINE]: "Working offline",
  [CollabState.CONNECTING]: "Establishing connection...",
  [CollabState.SYNCING]: "Synchronizing data...",
  [CollabState.ONLINE]: "Connected",
};

// ============================================================================
// CollabUI Class
// ============================================================================

/**
 * Creates and manages the collaboration UI elements
 */
export class CollabUI {
  private _container: HTMLElement;
  private _collabManager: CollabManager | null;
  private _presenceManager: PresenceManager | null;
  private _roomManager: RoomManager | null;

  private _collaborateBtn: HTMLButtonElement | null;
  private _statusDot: HTMLElement | null;
  private _detailsPanel: HTMLDivElement | null;
  private _shareTooltip: HTMLDivElement | null;

  private _currentState: SyncStateType;
  private _peerCount: number;
  private _showingDetails: boolean;
  private _linkCopied: boolean;
  private _onInitializeRequest: InitializeRequestCallback | null;

  constructor(options: CollabUIOptions) {
    if (!options.container) {
      throw new Error("CollabUI requires container option");
    }

    this._container = options.container;
    this._collabManager = options.collabManager ?? null;
    this._presenceManager = options.presenceManager ?? null;
    this._roomManager = options.roomManager ?? null;

    this._collaborateBtn = null;
    this._statusDot = null;
    this._detailsPanel = null;
    this._shareTooltip = null;

    this._currentState = CollabState.OFFLINE;
    this._peerCount = 0;
    this._showingDetails = false;
    this._linkCopied = false;
    this._onInitializeRequest = null;

    this._createElements();
    this._setupEventListeners();
  }

  /**
   * Set the CollabManager instance
   */
  setCollabManager(collabManager: CollabManager): void {
    this._collabManager = collabManager;
    this._setupCollabManagerListeners();
    this._updateState(collabManager.state);
    this._updateDebugModeCheckbox();
  }

  /**
   * Set the PresenceManager instance
   */
  setPresenceManager(presenceManager: PresenceManager): void {
    this._presenceManager = presenceManager;
    this._setupPresenceManagerListeners();
  }

  /**
   * Set the RoomManager instance
   */
  setRoomManager(roomManager: RoomManager): void {
    this._roomManager = roomManager;
  }

  /**
   * Set a callback to request collaboration initialization
   * Called when user clicks Copy Link before collaboration is ready
   */
  setOnInitializeRequest(callback: InitializeRequestCallback): void {
    this._onInitializeRequest = callback;
  }

  /**
   * Create the UI elements
   */
  private _createElements(): void {
    // Create "Collaborate" button with integrated status dot
    this._collaborateBtn = document.createElement("button");
    this._collaborateBtn.className = "collab-collaborate-btn";
    this._collaborateBtn.innerHTML = `
            <span>Collaborate</span>
            <span class="collab-status-dot" style="display: none;"></span>
        `;

    this._statusDot = this._collaborateBtn.querySelector(".collab-status-dot");

    // Create details panel (inside status badge for positioning)
    this._detailsPanel = document.createElement("div");
    this._detailsPanel.className = "collab-status-details";
    this._detailsPanel.innerHTML = `
            <div class="collab-status-details-header">Collaborate</div>
            <div class="collab-status-share-section">
                <p class="collab-share-description">Share this link to collaborate in real-time:</p>
                <button class="collab-copy-link-btn" id="collab-copy-link-btn">Copy Link</button>
            </div>
            <div class="collab-status-details-divider"></div>
            <div class="collab-status-details-row name-row">
                <span class="label">Your Name</span>
                <div class="collab-name-edit-inline">
                    <input type="text" id="collab-name-input" maxlength="20" placeholder="Enter your name">
                </div>
            </div>
            <div class="collab-status-details-divider" id="collab-connection-divider" style="display: none;"></div>
            <div class="collab-status-details-row" id="collab-status-row" style="display: none;">
                <span class="label">Status</span>
                <span class="value" id="collab-detail-status">Offline</span>
            </div>
            <div class="collab-status-details-row" id="collab-peers-row" style="display: none;">
                <span class="label">Peers</span>
                <span class="value" id="collab-detail-peers">0</span>
            </div>
            <div class="collab-status-details-row" id="collab-latency-row" style="display: none;">
                <span class="label">Latency</span>
                <span class="value" id="collab-detail-latency">-</span>
            </div>
            <div class="collab-status-details-row" id="collab-stats-row" style="display: none;">
                <span class="label">Sync</span>
                <span class="value" id="collab-detail-stats">-</span>
            </div>
            <div class="collab-status-details-peers" id="collab-peers-list" style="display: none;"></div>
            <div class="collab-status-details-actions" id="collab-actions" style="display: none;">
                <button class="reconnect-btn" id="collab-reconnect-btn">Force Reconnect</button>
            </div>
            <div class="collab-status-details-debug" id="collab-debug-section">
                <div class="debug-toggle">
                    <label>
                        <input type="checkbox" id="collab-debug-mode">
                        Debug mode
                    </label>
                </div>
                <div class="debug-actions" id="collab-debug-actions" style="display: none;">
                    <button class="debug-btn" id="collab-export-debug">Export Debug Data</button>
                    <button class="debug-btn danger" id="collab-reset-sync">Reset Sync State</button>
                </div>
            </div>
        `;
    // Append details panel to collaborate button
    this._collaborateBtn.appendChild(this._detailsPanel);

    // Create share tooltip
    this._shareTooltip = document.createElement("div");
    this._shareTooltip.className = "collab-share-tooltip";
    this._shareTooltip.textContent = "Link copied!";
    document.body.appendChild(this._shareTooltip);

    // Add button to container
    this._container.appendChild(this._collaborateBtn);

    // Initialize the name input with stored or generated name
    this._initializeNameInput();
  }

  /**
   * Get the initial display name from storage or generate a new one
   */
  private _getInitialDisplayName(): string {
    try {
      const storedName = sessionStorage.getItem("cells.displayName");
      if (storedName) {
        return storedName;
      }
    } catch {
      // sessionStorage not available
    }
    // Generate and store a new name
    const newName = generateRandomName();
    try {
      sessionStorage.setItem("cells.displayName", newName);
    } catch {
      // sessionStorage not available
    }
    return newName;
  }

  /**
   * Initialize the name input with the current display name
   */
  private _initializeNameInput(): void {
    const nameInput = this._detailsPanel?.querySelector(
      "#collab-name-input"
    ) as HTMLInputElement | null;
    if (nameInput) {
      nameInput.value = this._getInitialDisplayName();
    }
  }

  /**
   * Set up event listeners for UI interactions
   */
  private _setupEventListeners(): void {
    // Collaborate button opens the modal
    this._collaborateBtn?.addEventListener("click", (e) => {
      e.stopPropagation();
      this._toggleDetails();
    });

    // Prevent clicks inside details panel from closing it
    this._detailsPanel?.addEventListener("click", (e) => {
      e.stopPropagation();
    });

    // Close details when clicking outside
    document.addEventListener("click", (e) => {
      if (this._showingDetails && !this._collaborateBtn?.contains(e.target as Node)) {
        this._hideDetails();
      }
    });

    // Copy Link button click
    const copyLinkBtn = this._detailsPanel?.querySelector("#collab-copy-link-btn");
    if (copyLinkBtn) {
      copyLinkBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        void this._handleCopyLink();
      });
    }

    // Inline name input event handlers
    const nameInput = this._detailsPanel?.querySelector(
      "#collab-name-input"
    ) as HTMLInputElement | null;
    if (nameInput) {
      // Stop all keyboard events from propagating (prevents cell editing)
      nameInput.addEventListener("keydown", (e) => {
        e.stopPropagation();
        if (e.key === "Enter") {
          e.preventDefault();
          void this._saveDisplayName();
          nameInput.blur();
        } else if (e.key === "Escape") {
          e.preventDefault();
          // Restore original name
          if (this._presenceManager?.localName) {
            nameInput.value = this._presenceManager.localName;
          }
          nameInput.blur();
        }
      });

      nameInput.addEventListener("keyup", (e) => {
        e.stopPropagation();
      });

      nameInput.addEventListener("keypress", (e) => {
        e.stopPropagation();
      });

      // Save on blur
      nameInput.addEventListener("blur", () => {
        void this._saveDisplayName();
      });

      // Prevent input events from bubbling
      nameInput.addEventListener("input", (e) => {
        e.stopPropagation();
      });
    }

    // Force reconnect button
    const reconnectBtn = this._detailsPanel?.querySelector("#collab-reconnect-btn");
    if (reconnectBtn) {
      reconnectBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        void this._handleForceReconnect();
      });
    }

    // Debug mode toggle
    const debugToggle = this._detailsPanel?.querySelector(
      "#collab-debug-mode"
    ) as HTMLInputElement | null;
    if (debugToggle) {
      debugToggle.addEventListener("change", (e) => {
        e.stopPropagation();
        this._handleDebugModeToggle((e.target as HTMLInputElement).checked);
      });
      // Initialize checkbox state
      this._updateDebugModeCheckbox();
    }

    // Export debug data button
    const exportBtn = this._detailsPanel?.querySelector("#collab-export-debug");
    if (exportBtn) {
      exportBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        this._handleExportDebug();
      });
    }

    // Reset sync state button
    const resetBtn = this._detailsPanel?.querySelector("#collab-reset-sync");
    if (resetBtn) {
      resetBtn.addEventListener("click", (e) => {
        e.stopPropagation();
        void this._handleResetSync();
      });
    }
  }

  /**
   * Handle force reconnect button click
   */
  private async _handleForceReconnect(): Promise<void> {
    if (this._collabManager && typeof this._collabManager.forceReconnect === "function") {
      await this._collabManager.forceReconnect();
    }
  }

  /**
   * Handle debug mode toggle
   */
  private _handleDebugModeToggle(enabled: boolean): void {
    if (this._collabManager && typeof this._collabManager.setDebugMode === "function") {
      this._collabManager.setDebugMode(enabled);
    }
    this._updateDebugActionsVisibility();
  }

  /**
   * Handle export debug data button click
   */
  private _handleExportDebug(): void {
    if (this._collabManager && typeof this._collabManager.downloadDebugData === "function") {
      this._collabManager.downloadDebugData();
    }
  }

  /**
   * Handle reset sync state button click
   */
  private async _handleResetSync(): Promise<void> {
    if (confirm("Reset sync state? This will disconnect you from all peers.")) {
      if (this._collabManager && typeof this._collabManager.resetSyncState === "function") {
        await this._collabManager.resetSyncState();
      }
    }
  }

  /**
   * Update debug mode checkbox state
   */
  private _updateDebugModeCheckbox(): void {
    const checkbox = this._detailsPanel?.querySelector(
      "#collab-debug-mode"
    ) as HTMLInputElement | null;
    if (checkbox && this._collabManager) {
      checkbox.checked = this._collabManager.debugMode ?? false;
    }
    this._updateDebugActionsVisibility();
  }

  /**
   * Update debug actions visibility based on debug mode
   */
  private _updateDebugActionsVisibility(): void {
    const debugActions = this._detailsPanel?.querySelector(
      "#collab-debug-actions"
    ) as HTMLElement | null;
    const checkbox = this._detailsPanel?.querySelector(
      "#collab-debug-mode"
    ) as HTMLInputElement | null;
    if (debugActions && checkbox) {
      debugActions.style.display = checkbox.checked ? "" : "none";
    }
  }

  /**
   * Set up listeners for CollabManager events
   */
  private _setupCollabManagerListeners(): void {
    if (!this._collabManager) return;

    this._collabManager.on("statechange", (newState: unknown) => {
      this._updateState(newState as SyncStateType);
    });

    this._collabManager.on("latencyupdate", () => {
      this._updateLatencyDisplay();
    });
  }

  /**
   * Set up listeners for PresenceManager events
   */
  private _setupPresenceManagerListeners(): void {
    if (!this._presenceManager) return;

    this._presenceManager.on("peerarrived", () => {
      this._updatePeerCount();
      this._updatePeersList();
    });

    this._presenceManager.on("peerleft", () => {
      this._updatePeerCount();
      this._updatePeersList();
    });

    this._presenceManager.on("presenceupdated", () => {
      this._updatePeersList();
    });

    this._presenceManager.on("initialized", (data: unknown) => {
      const { name } = data as { name: string };
      this._updateLocalName(name);
    });

    this._presenceManager.on("localnamechanged", (name: unknown) => {
      this._updateLocalName(name as string);
    });

    // Update with current name if already initialized
    if (this._presenceManager.localName) {
      this._updateLocalName(this._presenceManager.localName);
    }
  }

  /**
   * Update the displayed local name
   */
  private _updateLocalName(name: string): void {
    const nameInput = this._detailsPanel?.querySelector(
      "#collab-name-input"
    ) as HTMLInputElement | null;
    if (nameInput && document.activeElement !== nameInput) {
      nameInput.value = name;
    }
  }

  /**
   * Save the display name from the inline input
   */
  private async _saveDisplayName(): Promise<void> {
    const nameInput = this._detailsPanel?.querySelector(
      "#collab-name-input"
    ) as HTMLInputElement | null;
    if (!nameInput) return;

    const newName = nameInput.value.trim();

    if (newName && this._presenceManager) {
      await this._presenceManager.setLocalName(newName);
    }
  }

  /**
   * Handle copy link button click
   * Creates/joins a room if needed and copies the share link
   */
  private async _handleCopyLink(): Promise<void> {
    const copyLinkBtn = this._detailsPanel?.querySelector(
      "#collab-copy-link-btn"
    ) as HTMLButtonElement | null;
    if (!copyLinkBtn) return;

    // Check if collaboration is set up
    if (!this._roomManager || !this._collabManager) {
      // Try to initialize if we have an initializer callback
      if (this._onInitializeRequest) {
        copyLinkBtn.textContent = "Initializing...";
        copyLinkBtn.disabled = true;
        try {
          await this._onInitializeRequest();
        } catch (err) {
          console.error("Failed to initialize collaboration:", err);
          copyLinkBtn.textContent = "Failed";
          copyLinkBtn.disabled = false;
          setTimeout(() => {
            copyLinkBtn.textContent = "Copy Link";
          }, 2000);
          return;
        }
        copyLinkBtn.disabled = false;
      }

      // Check again after initialization attempt
      if (!this._roomManager || !this._collabManager) {
        console.warn("Collaboration not configured");
        copyLinkBtn.textContent = "Loading...";
        setTimeout(() => {
          copyLinkBtn.textContent = "Copy Link";
        }, 2000);
        return;
      }
    }

    try {
      // If not in a room yet, create and join one
      if (!this._roomManager.currentRoomId) {
        copyLinkBtn.textContent = "Creating room...";
        copyLinkBtn.disabled = true;
        await this._roomManager.createAndJoinRoom();
      }

      // Generate share URL
      const roomId = this._collabManager.roomId;
      if (!roomId) {
        console.warn("Cannot share: not in a room");
        copyLinkBtn.textContent = "Copy Link";
        copyLinkBtn.disabled = false;
        return;
      }

      const url = new URL(window.location.href);
      url.searchParams.set("room", roomId);
      const shareUrl = url.toString();

      // Copy to clipboard
      await navigator.clipboard.writeText(shareUrl);

      // Mark link as copied - show status dot
      this._linkCopied = true;
      if (this._statusDot) {
        this._statusDot.style.display = "";
      }

      // Show success feedback
      copyLinkBtn.textContent = "Copied!";
      copyLinkBtn.disabled = false;
      setTimeout(() => {
        copyLinkBtn.textContent = "Copy Link";
      }, 2000);

      // Show tooltip
      this._showShareTooltip();
    } catch (err) {
      console.error("Failed to copy share link:", err);
      copyLinkBtn.textContent = "Failed to copy";
      copyLinkBtn.disabled = false;
      setTimeout(() => {
        copyLinkBtn.textContent = "Copy Link";
      }, 2000);
    }
  }

  /**
   * Update the UI state
   */
  private _updateState(state: SyncStateType): void {
    this._currentState = state;

    // Update status dot color class
    if (this._statusDot) {
      this._statusDot.classList.remove("offline", "connecting", "syncing", "online");
      this._statusDot.classList.add(state);
    }

    // Update details panel with status text
    const detailStatus = this._detailsPanel?.querySelector("#collab-detail-status");
    if (detailStatus) {
      detailStatus.textContent = DETAILED_STATUS_TEXT[state] ?? STATUS_TEXT[state] ?? "Unknown";
    }

    // Show connection info rows when we're in collaboration mode (link copied or joined via URL)
    const isInRoom = this._collabManager?.roomId != null;
    const showConnectionInfo = isInRoom || this._linkCopied;

    const connectionDivider = this._detailsPanel?.querySelector(
      "#collab-connection-divider"
    ) as HTMLElement | null;
    const statusRow = this._detailsPanel?.querySelector("#collab-status-row") as HTMLElement | null;
    const peersRow = this._detailsPanel?.querySelector("#collab-peers-row") as HTMLElement | null;

    if (connectionDivider) connectionDivider.style.display = showConnectionInfo ? "" : "none";
    if (statusRow) statusRow.style.display = showConnectionInfo ? "" : "none";
    if (peersRow) peersRow.style.display = showConnectionInfo ? "" : "none";

    // Show status dot when collaboration is active
    if (isInRoom || this._linkCopied) {
      if (this._statusDot) {
        this._statusDot.style.display = "";
      }
      this._linkCopied = true;
    }

    // Update latency display visibility
    this._updateLatencyDisplay();

    // Update actions panel visibility
    this._updateActionsVisibility();
  }

  /**
   * Update the peer count display
   */
  private _updatePeerCount(): void {
    if (!this._collabManager) return;

    this._peerCount = this._collabManager.getConnectedPeerCount();

    const detailPeers = this._detailsPanel?.querySelector("#collab-detail-peers");
    if (detailPeers) {
      detailPeers.textContent = this._peerCount.toString();
    }
  }

  /**
   * Update the latency display
   */
  private _updateLatencyDisplay(): void {
    if (!this._collabManager) return;

    const latencyRow = this._detailsPanel?.querySelector(
      "#collab-latency-row"
    ) as HTMLElement | null;
    const latencyValue = this._detailsPanel?.querySelector(
      "#collab-detail-latency"
    ) as HTMLElement | null;

    if (!latencyRow || !latencyValue) return;

    const avgLatency = this._collabManager.getAverageLatency();

    if (avgLatency !== null && this._currentState === CollabState.ONLINE) {
      latencyRow.style.display = "";
      const isPoor = avgLatency > 500;
      latencyValue.textContent = `${avgLatency}ms`;
      latencyValue.classList.toggle("poor", isPoor);
      if (isPoor) {
        latencyValue.title = "Connection quality is poor";
      } else {
        latencyValue.removeAttribute("title");
      }
    } else {
      latencyRow.style.display = "none";
    }
  }

  /**
   * Update the data transfer stats display
   */
  updateStats(): void {
    if (!this._collabManager) return;

    const statsRow = this._detailsPanel?.querySelector("#collab-stats-row") as HTMLElement | null;
    const statsValue = this._detailsPanel?.querySelector(
      "#collab-detail-stats"
    ) as HTMLElement | null;

    if (!statsRow || !statsValue) return;

    const stats = this._collabManager.stats;

    if (stats && this._currentState === CollabState.ONLINE) {
      statsRow.style.display = "";
      statsValue.textContent = `${stats.operationsSent} sent / ${stats.operationsReceived} recv`;
    } else {
      statsRow.style.display = "none";
    }
  }

  /**
   * Update the actions panel visibility
   */
  private _updateActionsVisibility(): void {
    const actionsPanel = this._detailsPanel?.querySelector("#collab-actions") as HTMLElement | null;
    if (!actionsPanel) return;

    // Show actions when connecting or online (to allow force reconnect)
    const showActions =
      this._currentState === CollabState.CONNECTING ||
      this._currentState === CollabState.SYNCING ||
      this._currentState === CollabState.ONLINE;

    actionsPanel.style.display = showActions ? "" : "none";
  }

  /**
   * Update the peers list in details panel
   */
  private _updatePeersList(): void {
    if (!this._presenceManager) return;

    const peersList = this._detailsPanel?.querySelector(
      "#collab-peers-list"
    ) as HTMLElement | null;
    if (!peersList) return;

    const remotePeers = this._presenceManager.getRemotePeers();

    if (remotePeers.size === 0) {
      peersList.style.display = "none";
      peersList.innerHTML = "";
      return;
    }

    peersList.style.display = "";
    peersList.innerHTML = "";

    for (const [, presence] of remotePeers) {
      const peerItem = document.createElement("div");
      peerItem.className = "peer-item";
      peerItem.innerHTML = `
                <span class="peer-color" style="background: ${presence.color}"></span>
                <span class="peer-name">${this._escapeHtml(presence.name)}</span>
            `;
      peersList.appendChild(peerItem);
    }
  }

  /**
   * Toggle the details panel visibility
   */
  private _toggleDetails(): void {
    if (this._showingDetails) {
      this._hideDetails();
    } else {
      this._showDetails();
    }
  }

  /**
   * Show the details panel
   */
  private _showDetails(): void {
    this._showingDetails = true;
    this._collaborateBtn?.classList.add("show-details");
    this._updatePeerCount();
    this._updatePeersList();
    this._updateState(this._currentState); // Refresh connection info visibility
  }

  /**
   * Hide the details panel
   */
  private _hideDetails(): void {
    this._showingDetails = false;
    this._collaborateBtn?.classList.remove("show-details");
  }

  /**
   * Show the share tooltip briefly
   */
  private _showShareTooltip(): void {
    // Position tooltip below copy link button
    const copyLinkBtn = this._detailsPanel?.querySelector("#collab-copy-link-btn");
    if (!copyLinkBtn || !this._shareTooltip) return;

    const btnRect = copyLinkBtn.getBoundingClientRect();
    this._shareTooltip.style.left = `${btnRect.left + btnRect.width / 2}px`;
    this._shareTooltip.style.top = `${btnRect.bottom + 8}px`;
    this._shareTooltip.style.transform = "translateX(-50%)";

    this._shareTooltip.classList.add("visible");

    // Hide after 2 seconds
    setTimeout(() => {
      this._shareTooltip?.classList.remove("visible");
    }, 2000);
  }

  /**
   * Escape HTML to prevent XSS
   */
  private _escapeHtml(str: string): string {
    const div = document.createElement("div");
    div.textContent = str;
    return div.innerHTML;
  }

  /**
   * Get the current room ID (for share link generation)
   */
  getRoomId(): RoomId | null {
    return this._collabManager?.roomId ?? null;
  }

  /**
   * Check if the link has been copied (collaboration started)
   */
  isCollaborating(): boolean {
    return this._linkCopied;
  }

  /**
   * Clean up resources
   */
  destroy(): void {
    if (this._shareTooltip?.parentNode) {
      this._shareTooltip.parentNode.removeChild(this._shareTooltip);
    }
  }
}
