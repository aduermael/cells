// Collaboration UI Module
// Manages the UI elements for collaboration status, share button, and peer indicators

import { CollabState } from './collab-manager.js';

/**
 * Status text for each collaboration state
 */
const STATUS_TEXT = {
    [CollabState.OFFLINE]: 'Offline',
    [CollabState.CONNECTING]: 'Connecting...',
    [CollabState.SYNCING]: 'Syncing...',
    [CollabState.ONLINE]: 'Online'
};

/**
 * Detailed status text shown in the details panel
 */
const DETAILED_STATUS_TEXT = {
    [CollabState.OFFLINE]: 'Working offline',
    [CollabState.CONNECTING]: 'Establishing connection...',
    [CollabState.SYNCING]: 'Synchronizing data...',
    [CollabState.ONLINE]: 'Connected'
};

/**
 * Creates and manages the collaboration UI elements
 */
export class CollabUI {
    /**
     * @param {Object} options
     * @param {HTMLElement} options.container - Container element to add UI elements to
     * @param {Object} [options.collabManager] - CollabManager instance (optional, can be set later)
     * @param {Object} [options.presenceManager] - PresenceManager instance (optional, can be set later)
     * @param {Object} [options.roomManager] - RoomManager instance (optional, can be set later)
     */
    constructor(options) {
        if (!options.container) {
            throw new Error('CollabUI requires container option');
        }

        this._container = options.container;
        this._collabManager = options.collabManager || null;
        this._presenceManager = options.presenceManager || null;
        this._roomManager = options.roomManager || null;

        this._collaborateBtn = null;
        this._statusDot = null;
        this._detailsPanel = null;
        this._shareTooltip = null;

        this._currentState = CollabState.OFFLINE;
        this._peerCount = 0;
        this._showingDetails = false;
        this._linkCopied = false; // Track if link has been copied at least once
        this._onInitializeRequest = null; // Callback to initialize collaboration on demand

        this._createElements();
        this._setupEventListeners();
    }

    /**
     * Set the CollabManager instance
     * @param {Object} collabManager
     */
    setCollabManager(collabManager) {
        this._collabManager = collabManager;
        this._setupCollabManagerListeners();
        this._updateState(collabManager.state);
        this._updateDebugModeCheckbox();
    }

    /**
     * Set the PresenceManager instance
     * @param {Object} presenceManager
     */
    setPresenceManager(presenceManager) {
        this._presenceManager = presenceManager;
        this._setupPresenceManagerListeners();
    }

    /**
     * Set the RoomManager instance
     * @param {Object} roomManager
     */
    setRoomManager(roomManager) {
        this._roomManager = roomManager;
    }

    /**
     * Set a callback to request collaboration initialization
     * Called when user clicks Copy Link before collaboration is ready
     * @param {Function} callback - Async function to initialize collaboration
     */
    setOnInitializeRequest(callback) {
        this._onInitializeRequest = callback;
    }

    /**
     * Create the UI elements
     * @private
     */
    _createElements() {
        // Create "Collaborate" button with integrated status dot
        this._collaborateBtn = document.createElement('button');
        this._collaborateBtn.className = 'collab-collaborate-btn';
        this._collaborateBtn.innerHTML = `
            <span>Collaborate</span>
            <span class="collab-status-dot" style="display: none;"></span>
        `;

        this._statusDot = this._collaborateBtn.querySelector('.collab-status-dot');

        // Create details panel (inside status badge for positioning)
        this._detailsPanel = document.createElement('div');
        this._detailsPanel.className = 'collab-status-details';
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
            <div class="collab-status-details-row" id="collab-pending-row" style="display: none;">
                <span class="label">Pending</span>
                <span class="value" id="collab-detail-pending">0 edits</span>
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
        this._shareTooltip = document.createElement('div');
        this._shareTooltip.className = 'collab-share-tooltip';
        this._shareTooltip.textContent = 'Link copied!';
        document.body.appendChild(this._shareTooltip);

        // Add button to container
        this._container.appendChild(this._collaborateBtn);
    }

    /**
     * Set up event listeners for UI interactions
     * @private
     */
    _setupEventListeners() {
        // Collaborate button opens the modal
        this._collaborateBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            this._toggleDetails();
        });

        // Prevent clicks inside details panel from closing it
        this._detailsPanel.addEventListener('click', (e) => {
            e.stopPropagation();
        });

        // Close details when clicking outside
        document.addEventListener('click', (e) => {
            if (this._showingDetails && !this._collaborateBtn.contains(e.target)) {
                this._hideDetails();
            }
        });

        // Copy Link button click
        const copyLinkBtn = this._detailsPanel.querySelector('#collab-copy-link-btn');
        if (copyLinkBtn) {
            copyLinkBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                this._handleCopyLink();
            });
        }

        // Inline name input event handlers
        const nameInput = this._detailsPanel.querySelector('#collab-name-input');
        if (nameInput) {
            // Stop all keyboard events from propagating (prevents cell editing)
            nameInput.addEventListener('keydown', (e) => {
                e.stopPropagation();
                if (e.key === 'Enter') {
                    e.preventDefault();
                    this._saveDisplayName();
                    nameInput.blur();
                } else if (e.key === 'Escape') {
                    e.preventDefault();
                    // Restore original name
                    if (this._presenceManager && this._presenceManager.localName) {
                        nameInput.value = this._presenceManager.localName;
                    }
                    nameInput.blur();
                }
            });

            nameInput.addEventListener('keyup', (e) => {
                e.stopPropagation();
            });

            nameInput.addEventListener('keypress', (e) => {
                e.stopPropagation();
            });

            // Save on blur
            nameInput.addEventListener('blur', () => {
                this._saveDisplayName();
            });

            // Prevent input events from bubbling
            nameInput.addEventListener('input', (e) => {
                e.stopPropagation();
            });
        }

        // Force reconnect button
        const reconnectBtn = this._detailsPanel.querySelector('#collab-reconnect-btn');
        if (reconnectBtn) {
            reconnectBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                this._handleForceReconnect();
            });
        }

        // Debug mode toggle
        const debugToggle = this._detailsPanel.querySelector('#collab-debug-mode');
        if (debugToggle) {
            debugToggle.addEventListener('change', (e) => {
                e.stopPropagation();
                this._handleDebugModeToggle(e.target.checked);
            });
            // Initialize checkbox state
            this._updateDebugModeCheckbox();
        }

        // Export debug data button
        const exportBtn = this._detailsPanel.querySelector('#collab-export-debug');
        if (exportBtn) {
            exportBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                this._handleExportDebug();
            });
        }

        // Reset sync state button
        const resetBtn = this._detailsPanel.querySelector('#collab-reset-sync');
        if (resetBtn) {
            resetBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                this._handleResetSync();
            });
        }
    }

    /**
     * Handle force reconnect button click
     * @private
     */
    _handleForceReconnect() {
        if (this._collabManager && typeof this._collabManager.forceReconnect === 'function') {
            this._collabManager.forceReconnect();
        }
    }

    /**
     * Handle debug mode toggle
     * @param {boolean} enabled
     * @private
     */
    _handleDebugModeToggle(enabled) {
        if (this._collabManager && typeof this._collabManager.setDebugMode === 'function') {
            this._collabManager.setDebugMode(enabled);
        }
        this._updateDebugActionsVisibility();
    }

    /**
     * Handle export debug data button click
     * @private
     */
    _handleExportDebug() {
        if (this._collabManager && typeof this._collabManager.downloadDebugData === 'function') {
            this._collabManager.downloadDebugData();
        }
    }

    /**
     * Handle reset sync state button click
     * @private
     */
    _handleResetSync() {
        if (confirm('Reset sync state? This will disconnect you from all peers.')) {
            if (this._collabManager && typeof this._collabManager.resetSyncState === 'function') {
                this._collabManager.resetSyncState();
            }
        }
    }

    /**
     * Update debug mode checkbox state
     * @private
     */
    _updateDebugModeCheckbox() {
        const checkbox = this._detailsPanel.querySelector('#collab-debug-mode');
        if (checkbox && this._collabManager) {
            checkbox.checked = this._collabManager.debugMode || false;
        }
        this._updateDebugActionsVisibility();
    }

    /**
     * Update debug actions visibility based on debug mode
     * @private
     */
    _updateDebugActionsVisibility() {
        const debugActions = this._detailsPanel.querySelector('#collab-debug-actions');
        const checkbox = this._detailsPanel.querySelector('#collab-debug-mode');
        if (debugActions && checkbox) {
            debugActions.style.display = checkbox.checked ? '' : 'none';
        }
    }

    /**
     * Set up listeners for CollabManager events
     * @private
     */
    _setupCollabManagerListeners() {
        if (!this._collabManager) return;

        this._collabManager.on('statechange', (newState, oldState) => {
            this._updateState(newState);
        });

        this._collabManager.on('latencyupdate', (peerId, latency) => {
            this._updateLatencyDisplay();
        });
    }

    /**
     * Set up listeners for PresenceManager events
     * @private
     */
    _setupPresenceManagerListeners() {
        if (!this._presenceManager) return;

        this._presenceManager.on('peerarrived', () => {
            this._updatePeerCount();
            this._updatePeersList();
        });

        this._presenceManager.on('peerleft', () => {
            this._updatePeerCount();
            this._updatePeersList();
        });

        this._presenceManager.on('presenceupdated', () => {
            this._updatePeersList();
        });

        this._presenceManager.on('initialized', ({ name }) => {
            this._updateLocalName(name);
        });

        this._presenceManager.on('localnamechanged', (name) => {
            this._updateLocalName(name);
        });

        // Update with current name if already initialized
        if (this._presenceManager.localName) {
            this._updateLocalName(this._presenceManager.localName);
        }
    }

    /**
     * Update the displayed local name
     * @param {string} name
     * @private
     */
    _updateLocalName(name) {
        const nameInput = this._detailsPanel.querySelector('#collab-name-input');
        if (nameInput && document.activeElement !== nameInput) {
            nameInput.value = name;
        }
    }

    /**
     * Save the display name from the inline input
     * @private
     */
    _saveDisplayName() {
        const nameInput = this._detailsPanel.querySelector('#collab-name-input');
        if (!nameInput) return;

        const newName = nameInput.value.trim();

        if (newName && this._presenceManager) {
            this._presenceManager.setLocalName(newName);
        }
    }

    /**
     * Handle copy link button click
     * Creates/joins a room if needed and copies the share link
     * @private
     */
    async _handleCopyLink() {
        const copyLinkBtn = this._detailsPanel.querySelector('#collab-copy-link-btn');

        // Check if collaboration is set up
        if (!this._roomManager || !this._collabManager) {
            // Try to initialize if we have an initializer callback
            if (this._onInitializeRequest) {
                copyLinkBtn.textContent = 'Initializing...';
                copyLinkBtn.disabled = true;
                try {
                    await this._onInitializeRequest();
                } catch (err) {
                    console.error('Failed to initialize collaboration:', err);
                    copyLinkBtn.textContent = 'Failed';
                    copyLinkBtn.disabled = false;
                    setTimeout(() => {
                        copyLinkBtn.textContent = 'Copy Link';
                    }, 2000);
                    return;
                }
                copyLinkBtn.disabled = false;
            }

            // Check again after initialization attempt
            if (!this._roomManager || !this._collabManager) {
                console.warn('Collaboration not configured');
                copyLinkBtn.textContent = 'Loading...';
                setTimeout(() => {
                    copyLinkBtn.textContent = 'Copy Link';
                }, 2000);
                return;
            }
        }

        try {
            // If not in a room yet, create and join one
            if (!this._roomManager.currentRoomId) {
                copyLinkBtn.textContent = 'Creating room...';
                copyLinkBtn.disabled = true;
                await this._roomManager.createAndJoinRoom();
            }

            // Generate share URL
            const roomId = this._collabManager.roomId;
            if (!roomId) {
                console.warn('Cannot share: not in a room');
                copyLinkBtn.textContent = 'Copy Link';
                copyLinkBtn.disabled = false;
                return;
            }

            const url = new URL(window.location.href);
            url.searchParams.set('room', roomId);
            const shareUrl = url.toString();

            // Copy to clipboard
            await navigator.clipboard.writeText(shareUrl);

            // Mark link as copied - show status dot
            this._linkCopied = true;
            this._statusDot.style.display = '';

            // Show success feedback
            copyLinkBtn.textContent = 'Copied!';
            copyLinkBtn.disabled = false;
            setTimeout(() => {
                copyLinkBtn.textContent = 'Copy Link';
            }, 2000);

            // Show tooltip
            this._showShareTooltip();

        } catch (err) {
            console.error('Failed to copy share link:', err);
            copyLinkBtn.textContent = 'Failed to copy';
            copyLinkBtn.disabled = false;
            setTimeout(() => {
                copyLinkBtn.textContent = 'Copy Link';
            }, 2000);
        }
    }

    /**
     * Update the UI state
     * @param {string} state - CollabState value
     * @private
     */
    _updateState(state) {
        this._currentState = state;

        // Update status dot color class
        this._statusDot.classList.remove('offline', 'connecting', 'syncing', 'online');
        this._statusDot.classList.add(state);

        // Update details panel with status text
        const detailStatus = this._detailsPanel.querySelector('#collab-detail-status');
        if (detailStatus) {
            detailStatus.textContent = DETAILED_STATUS_TEXT[state] || STATUS_TEXT[state] || 'Unknown';
        }

        // Show connection info rows when we're in collaboration mode (link copied or joined via URL)
        const isInRoom = this._collabManager && this._collabManager.roomId;
        const showConnectionInfo = isInRoom || this._linkCopied;

        const connectionDivider = this._detailsPanel.querySelector('#collab-connection-divider');
        const statusRow = this._detailsPanel.querySelector('#collab-status-row');
        const peersRow = this._detailsPanel.querySelector('#collab-peers-row');

        if (connectionDivider) connectionDivider.style.display = showConnectionInfo ? '' : 'none';
        if (statusRow) statusRow.style.display = showConnectionInfo ? '' : 'none';
        if (peersRow) peersRow.style.display = showConnectionInfo ? '' : 'none';

        // Show status dot when collaboration is active
        if (isInRoom || this._linkCopied) {
            this._statusDot.style.display = '';
            this._linkCopied = true;
        }

        // Update latency display visibility
        this._updateLatencyDisplay();

        // Update actions panel visibility
        this._updateActionsVisibility();
    }

    /**
     * Update the peer count display
     * @private
     */
    _updatePeerCount() {
        if (!this._collabManager) return;

        this._peerCount = this._collabManager.getConnectedPeerCount();

        const detailPeers = this._detailsPanel.querySelector('#collab-detail-peers');
        if (detailPeers) {
            detailPeers.textContent = this._peerCount.toString();
        }
    }

    /**
     * Update the pending operations count display
     * @param {number} count - Number of pending operations
     */
    updatePendingCount(count) {
        const pendingRow = this._detailsPanel.querySelector('#collab-pending-row');
        const pendingValue = this._detailsPanel.querySelector('#collab-detail-pending');

        if (!pendingRow || !pendingValue) return;

        if (count > 0 && this._currentState !== CollabState.ONLINE) {
            pendingRow.style.display = '';
            const editText = count === 1 ? 'edit' : 'edits';
            pendingValue.textContent = `${count} ${editText}`;
        } else {
            pendingRow.style.display = 'none';
        }
    }

    /**
     * Update the latency display
     * @private
     */
    _updateLatencyDisplay() {
        if (!this._collabManager) return;

        const latencyRow = this._detailsPanel.querySelector('#collab-latency-row');
        const latencyValue = this._detailsPanel.querySelector('#collab-detail-latency');

        if (!latencyRow || !latencyValue) return;

        const avgLatency = this._collabManager.getAverageLatency();

        if (avgLatency !== null && this._currentState === CollabState.ONLINE) {
            latencyRow.style.display = '';
            const isPoor = avgLatency > 500;
            latencyValue.textContent = `${avgLatency}ms`;
            latencyValue.classList.toggle('poor', isPoor);
            if (isPoor) {
                latencyValue.title = 'Connection quality is poor';
            } else {
                latencyValue.removeAttribute('title');
            }
        } else {
            latencyRow.style.display = 'none';
        }
    }

    /**
     * Update the data transfer stats display
     */
    updateStats() {
        if (!this._collabManager) return;

        const statsRow = this._detailsPanel.querySelector('#collab-stats-row');
        const statsValue = this._detailsPanel.querySelector('#collab-detail-stats');

        if (!statsRow || !statsValue) return;

        const stats = this._collabManager.stats;

        if (stats && this._currentState === CollabState.ONLINE) {
            statsRow.style.display = '';
            statsValue.textContent = `${stats.operationsSent} sent / ${stats.operationsReceived} recv`;
        } else {
            statsRow.style.display = 'none';
        }
    }

    /**
     * Update the actions panel visibility
     * @private
     */
    _updateActionsVisibility() {
        const actionsPanel = this._detailsPanel.querySelector('#collab-actions');
        if (!actionsPanel) return;

        // Show actions when connecting or online (to allow force reconnect)
        const showActions = this._currentState === CollabState.CONNECTING ||
                           this._currentState === CollabState.SYNCING ||
                           this._currentState === CollabState.ONLINE;

        actionsPanel.style.display = showActions ? '' : 'none';
    }

    /**
     * Update the peers list in details panel
     * @private
     */
    _updatePeersList() {
        if (!this._presenceManager) return;

        const peersList = this._detailsPanel.querySelector('#collab-peers-list');
        if (!peersList) return;

        const remotePeers = this._presenceManager.getRemotePeers();

        if (remotePeers.size === 0) {
            peersList.style.display = 'none';
            peersList.innerHTML = '';
            return;
        }

        peersList.style.display = '';
        peersList.innerHTML = '';

        for (const [peerId, presence] of remotePeers) {
            const peerItem = document.createElement('div');
            peerItem.className = 'peer-item';
            peerItem.innerHTML = `
                <span class="peer-color" style="background: ${presence.color}"></span>
                <span class="peer-name">${this._escapeHtml(presence.name)}</span>
            `;
            peersList.appendChild(peerItem);
        }
    }

    /**
     * Toggle the details panel visibility
     * @private
     */
    _toggleDetails() {
        if (this._showingDetails) {
            this._hideDetails();
        } else {
            this._showDetails();
        }
    }

    /**
     * Show the details panel
     * @private
     */
    _showDetails() {
        this._showingDetails = true;
        this._collaborateBtn.classList.add('show-details');
        this._updatePeerCount();
        this._updatePeersList();
        this._updateState(this._currentState); // Refresh connection info visibility
    }

    /**
     * Hide the details panel
     * @private
     */
    _hideDetails() {
        this._showingDetails = false;
        this._collaborateBtn.classList.remove('show-details');
    }

    /**
     * Show the share tooltip briefly
     * @private
     */
    _showShareTooltip() {
        // Position tooltip below copy link button
        const copyLinkBtn = this._detailsPanel.querySelector('#collab-copy-link-btn');
        if (!copyLinkBtn) return;

        const btnRect = copyLinkBtn.getBoundingClientRect();
        this._shareTooltip.style.left = `${btnRect.left + btnRect.width / 2}px`;
        this._shareTooltip.style.top = `${btnRect.bottom + 8}px`;
        this._shareTooltip.style.transform = 'translateX(-50%)';

        this._shareTooltip.classList.add('visible');

        // Hide after 2 seconds
        setTimeout(() => {
            this._shareTooltip.classList.remove('visible');
        }, 2000);
    }

    /**
     * Escape HTML to prevent XSS
     * @param {string} str
     * @returns {string}
     * @private
     */
    _escapeHtml(str) {
        const div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    }

    /**
     * Get the current room ID (for share link generation)
     * @returns {string|null}
     */
    getRoomId() {
        return this._collabManager ? this._collabManager.roomId : null;
    }

    /**
     * Check if the link has been copied (collaboration started)
     * @returns {boolean}
     */
    isCollaborating() {
        return this._linkCopied;
    }

    /**
     * Clean up resources
     */
    destroy() {
        if (this._shareTooltip && this._shareTooltip.parentNode) {
            this._shareTooltip.parentNode.removeChild(this._shareTooltip);
        }
    }
}
