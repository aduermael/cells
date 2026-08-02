// =============================================================================
// Collaboration UI
// =============================================================================
//
// UI components for real-time collaboration: status indicator, share button,
// peer list, nickname, debug mode, and version footer.
//
// This is a UI-ONLY module. All data mutations go through CRDT operations
// in the C++ core via the WASM bridge. Panel markup comes from
// collab-menu-content.ts (pure, unit-tested).
//
// Key responsibilities:
// - Status dot: shows connection state (connected/connecting/disconnected)
// - Share button: copies room link
// - Nickname input: local display name
// - Status / peers: connection state and remote collaborators
// - Debug mode: export debug data / reset sync
// - Version label at the bottom of the panel
//
// Coordinates with:
// - CppSyncAdapter: sync state, peer presence, connection control
// - RoomManager: room creation and URL management
// - PresenceBroadcaster: local cursor/selection broadcasting
//
// =============================================================================

import { SyncState } from "./cpp-sync-adapter";
import { generateRandomName } from "./presence";
import { getMenuStateManager } from "./menu-state";
import { buildCollabDetailsHtml } from "./collab-menu-content";
import type { SyncStateType, PeerPresence, RoomId } from "./types";

// ============================================================================
// Types
// ============================================================================

/** Collab manager interface (subset of CppSyncAdapter) */
interface CollabManager {
    state: SyncStateType;
    roomId: RoomId | null;
    debugMode: boolean;
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
    createAndJoinRoom(): Promise<RoomId>;
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
    [SyncState.OFFLINE]: "Offline",
    [SyncState.CONNECTING]: "Connecting...",
    [SyncState.SYNCING]: "Syncing...",
    [SyncState.ONLINE]: "Online",
};

/**
 * Detailed status text shown in the details panel
 */
const DETAILED_STATUS_TEXT: Record<string, string> = {
    [SyncState.OFFLINE]: "Working offline",
    [SyncState.CONNECTING]: "Establishing connection...",
    [SyncState.SYNCING]: "Synchronizing data...",
    [SyncState.ONLINE]: "Connected",
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

        this._currentState = SyncState.OFFLINE;
        this._peerCount = 0;
        this._showingDetails = false;
        this._linkCopied = false;
        this._onInitializeRequest = null;

        this._createElements();
        this._setupEventListeners();
        this._registerWithMenuState();
    }

    /**
     * Register with the global menu state manager
     */
    private _registerWithMenuState(): void {
        const menuState = getMenuStateManager();
        menuState.registerMenu("collaborate", () => {
            this._hideDetails();
        });
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
        this._collaborateBtn.className = "btn btn-icon collab-collaborate-btn";
        this._collaborateBtn.title = "Collaborate";
        this._collaborateBtn.innerHTML = `
            <svg class="btn-icon-svg" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/>
                <circle cx="9" cy="7" r="4"/>
                <path d="M23 21v-2a4 4 0 0 0-3-3.87"/>
                <path d="M16 3.13a4 4 0 0 1 0 7.75"/>
            </svg>
            <span class="btn-label">Collaborate</span>
            <span class="collab-status-dot" style="display: none;"></span>
        `;

        this._statusDot =
            this._collaborateBtn.querySelector(".collab-status-dot");

        // Create details panel (share, nickname, status, peers, debug, version)
        this._detailsPanel = document.createElement("div");
        this._detailsPanel.className = "collab-status-details";
        this._detailsPanel.innerHTML = buildCollabDetailsHtml();
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
            const storedName = localStorage.getItem("cells.displayName");
            if (storedName) {
                return storedName;
            }
        } catch {
            // localStorage not available
        }
        // Generate and store a new name
        const newName = generateRandomName();
        try {
            localStorage.setItem("cells.displayName", newName);
        } catch {
            // localStorage not available
        }
        return newName;
    }

    /**
     * Initialize the name input with the current display name
     */
    private _initializeNameInput(): void {
        const nameInput = this._detailsPanel?.querySelector(
            "#collab-name-input",
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
            if (
                this._showingDetails &&
                !this._collaborateBtn?.contains(e.target as Node)
            ) {
                this._hideDetails();
            }
        });

        // Copy Link button click
        const copyLinkBtn = this._detailsPanel?.querySelector(
            "#collab-copy-link-btn",
        );
        if (copyLinkBtn) {
            copyLinkBtn.addEventListener("click", (e) => {
                e.stopPropagation();
                void this._handleCopyLink();
            });
        }

        // Inline name input event handlers
        const nameInput = this._detailsPanel?.querySelector(
            "#collab-name-input",
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

        // Debug mode toggle
        const debugToggle = this._detailsPanel?.querySelector(
            "#collab-debug-mode",
        ) as HTMLInputElement | null;
        if (debugToggle) {
            debugToggle.addEventListener("change", (e) => {
                e.stopPropagation();
                this._handleDebugModeToggle(
                    (e.target as HTMLInputElement).checked,
                );
            });
            // Initialize checkbox state
            this._updateDebugModeCheckbox();
        }

        // Force reconnect (debug only)
        const reconnectBtn = this._detailsPanel?.querySelector(
            "#collab-reconnect-btn",
        );
        if (reconnectBtn) {
            reconnectBtn.addEventListener("click", (e) => {
                e.stopPropagation();
                void this._handleForceReconnect();
            });
        }

        // Export debug data button
        const exportBtn = this._detailsPanel?.querySelector(
            "#collab-export-debug",
        );
        if (exportBtn) {
            exportBtn.addEventListener("click", (e) => {
                e.stopPropagation();
                this._handleExportDebug();
            });
        }

        // Reset sync state button
        const resetBtn =
            this._detailsPanel?.querySelector("#collab-reset-sync");
        if (resetBtn) {
            resetBtn.addEventListener("click", (e) => {
                e.stopPropagation();
                void this._handleResetSync();
            });
        }
    }

    /**
     * Handle force reconnect button click (debug mode)
     */
    private async _handleForceReconnect(): Promise<void> {
        if (
            this._collabManager &&
            typeof this._collabManager.forceReconnect === "function"
        ) {
            await this._collabManager.forceReconnect();
        }
    }

    /**
     * Handle debug mode toggle
     */
    private _handleDebugModeToggle(enabled: boolean): void {
        if (
            this._collabManager &&
            typeof this._collabManager.setDebugMode === "function"
        ) {
            this._collabManager.setDebugMode(enabled);
        }
        this._updateDebugActionsVisibility();
        if (enabled) {
            this._updateLatencyDisplay();
        }
    }

    /**
     * Handle export debug data button click
     */
    private _handleExportDebug(): void {
        if (
            this._collabManager &&
            typeof this._collabManager.downloadDebugData === "function"
        ) {
            this._collabManager.downloadDebugData();
        }
    }

    /**
     * Handle reset sync state button click
     */
    private async _handleResetSync(): Promise<void> {
        if (
            confirm(
                "Reset sync state? This will disconnect you from all peers.",
            )
        ) {
            if (
                this._collabManager &&
                typeof this._collabManager.resetSyncState === "function"
            ) {
                await this._collabManager.resetSyncState();
            }
        }
    }

    /**
     * Update debug mode checkbox state
     */
    private _updateDebugModeCheckbox(): void {
        const checkbox = this._detailsPanel?.querySelector(
            "#collab-debug-mode",
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
            "#collab-debug-actions",
        ) as HTMLElement | null;
        const checkbox = this._detailsPanel?.querySelector(
            "#collab-debug-mode",
        ) as HTMLInputElement | null;
        if (debugActions && checkbox) {
            debugActions.style.display = checkbox.checked ? "" : "none";
        }
        if (checkbox?.checked) {
            this._updateLatencyDisplay();
        }
    }

    /**
     * Whether debug mode is currently enabled in the UI
     */
    private _isDebugModeEnabled(): boolean {
        const checkbox = this._detailsPanel?.querySelector(
            "#collab-debug-mode",
        ) as HTMLInputElement | null;
        return checkbox?.checked ?? false;
    }

    /**
     * Update the latency display (debug mode only)
     */
    private _updateLatencyDisplay(): void {
        if (!this._collabManager || !this._isDebugModeEnabled()) return;

        const latencyValue = this._detailsPanel?.querySelector(
            "#collab-detail-latency",
        ) as HTMLElement | null;
        if (!latencyValue) return;

        const avgLatency = this._collabManager.getAverageLatency();

        if (avgLatency !== null && this._currentState === SyncState.ONLINE) {
            const isPoor = avgLatency > 500;
            latencyValue.textContent = `${avgLatency}ms`;
            latencyValue.classList.toggle("poor", isPoor);
            if (isPoor) {
                latencyValue.title = "Connection quality is poor";
            } else {
                latencyValue.removeAttribute("title");
            }
        } else {
            latencyValue.textContent = "-";
            latencyValue.classList.remove("poor");
            latencyValue.removeAttribute("title");
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
            "#collab-name-input",
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
            "#collab-name-input",
        ) as HTMLInputElement | null;
        if (!nameInput) return;

        const newName = nameInput.value.trim();

        if (newName) {
            // Persist to localStorage for future sessions
            try {
                localStorage.setItem("cells.displayName", newName);
            } catch {
                // localStorage not available
            }

            if (this._presenceManager) {
                await this._presenceManager.setLocalName(newName);
            }
        }
    }

    /**
     * Handle copy link button click
     * Creates/joins a room if needed and copies the share link
     */
    private async _handleCopyLink(): Promise<void> {
        const copyLinkBtn = this._detailsPanel?.querySelector(
            "#collab-copy-link-btn",
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
            this._statusDot.classList.remove(
                "offline",
                "connecting",
                "syncing",
                "online",
            );
            this._statusDot.classList.add(state);
        }

        // Update details panel with status text
        const detailStatus = this._detailsPanel?.querySelector(
            "#collab-detail-status",
        );
        if (detailStatus) {
            detailStatus.textContent =
                DETAILED_STATUS_TEXT[state] ?? STATUS_TEXT[state] ?? "Unknown";
        }

        // Show status dot when collaboration is active (in a room or link copied)
        const isInRoom = this._collabManager?.roomId != null;
        if (isInRoom || this._linkCopied) {
            if (this._statusDot) {
                this._statusDot.style.display = "";
            }
            this._linkCopied = true;
        }

        // Refresh latency when online state changes (debug mode)
        this._updateLatencyDisplay();
    }

    /**
     * Update the peer count display
     */
    private _updatePeerCount(): void {
        if (!this._collabManager) return;

        this._peerCount = this._collabManager.getConnectedPeerCount();

        const detailPeers = this._detailsPanel?.querySelector(
            "#collab-detail-peers",
        );
        if (detailPeers) {
            detailPeers.textContent = this._peerCount.toString();
        }
    }

    /**
     * Update the peers list in details panel
     */
    private _updatePeersList(): void {
        if (!this._presenceManager) return;

        const peersList = this._detailsPanel?.querySelector(
            "#collab-peers-list",
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
        const menuState = getMenuStateManager();
        menuState.openMenu("collaborate"); // This closes other menus
        this._showingDetails = true;
        this._collaborateBtn?.classList.add("show-details");
        this._updatePeerCount();
        this._updatePeersList();
        this._updateState(this._currentState);
        this._updateLatencyDisplay();
    }

    /**
     * Hide the details panel
     */
    private _hideDetails(): void {
        this._showingDetails = false;
        this._collaborateBtn?.classList.remove("show-details");
        const menuState = getMenuStateManager();
        menuState.closeMenu("collaborate");
    }

    /**
     * Show the share tooltip briefly
     */
    private _showShareTooltip(): void {
        // Position tooltip below copy link button
        const copyLinkBtn = this._detailsPanel?.querySelector(
            "#collab-copy-link-btn",
        );
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
        // Unregister from menu state manager
        const menuState = getMenuStateManager();
        menuState.unregisterMenu("collaborate");
    }
}
