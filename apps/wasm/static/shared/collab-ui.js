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
 * Creates and manages the collaboration UI elements
 */
export class CollabUI {
    /**
     * @param {Object} options
     * @param {HTMLElement} options.container - Container element to add UI elements to
     * @param {Object} [options.collabManager] - CollabManager instance (optional, can be set later)
     * @param {Object} [options.presenceManager] - PresenceManager instance (optional, can be set later)
     */
    constructor(options) {
        if (!options.container) {
            throw new Error('CollabUI requires container option');
        }

        this._container = options.container;
        this._collabManager = options.collabManager || null;
        this._presenceManager = options.presenceManager || null;

        this._statusBadge = null;
        this._statusDot = null;
        this._statusText = null;
        this._detailsPanel = null;
        this._shareBtn = null;
        this._shareTooltip = null;

        this._currentState = CollabState.OFFLINE;
        this._peerCount = 0;
        this._showingDetails = false;

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
     * Create the UI elements
     * @private
     */
    _createElements() {
        // Create status badge
        this._statusBadge = document.createElement('div');
        this._statusBadge.className = 'collab-status-badge offline';
        this._statusBadge.innerHTML = `
            <span class="status-dot"></span>
            <span class="status-text">Offline</span>
        `;

        this._statusDot = this._statusBadge.querySelector('.status-dot');
        this._statusText = this._statusBadge.querySelector('.status-text');

        // Create details panel (inside status badge for positioning)
        this._detailsPanel = document.createElement('div');
        this._detailsPanel.className = 'collab-status-details';
        this._detailsPanel.innerHTML = `
            <div class="collab-status-details-header">Connection Status</div>
            <div class="collab-status-details-row">
                <span class="label">Status</span>
                <span class="value" id="collab-detail-status">Offline</span>
            </div>
            <div class="collab-status-details-row">
                <span class="label">Peers</span>
                <span class="value" id="collab-detail-peers">0</span>
            </div>
            <div class="collab-status-details-peers" id="collab-peers-list" style="display: none;"></div>
        `;
        this._statusBadge.appendChild(this._detailsPanel);

        // Create share button
        this._shareBtn = document.createElement('button');
        this._shareBtn.className = 'collab-share-btn';
        this._shareBtn.innerHTML = `
            <span class="share-icon">🔗</span>
            <span>Share</span>
        `;
        this._shareBtn.style.display = 'none'; // Hidden until collaboration is enabled

        // Create share tooltip
        this._shareTooltip = document.createElement('div');
        this._shareTooltip.className = 'collab-share-tooltip';
        this._shareTooltip.textContent = 'Link copied!';
        document.body.appendChild(this._shareTooltip);

        // Add elements to container
        this._container.appendChild(this._shareBtn);
        this._container.appendChild(this._statusBadge);
    }

    /**
     * Set up event listeners for UI interactions
     * @private
     */
    _setupEventListeners() {
        // Toggle details on status badge click
        this._statusBadge.addEventListener('click', (e) => {
            e.stopPropagation();
            this._toggleDetails();
        });

        // Close details when clicking outside
        document.addEventListener('click', (e) => {
            if (this._showingDetails && !this._statusBadge.contains(e.target)) {
                this._hideDetails();
            }
        });

        // Share button click
        this._shareBtn.addEventListener('click', () => {
            this._handleShare();
        });
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
    }

    /**
     * Update the UI state
     * @param {string} state - CollabState value
     * @private
     */
    _updateState(state) {
        this._currentState = state;

        // Update badge class
        this._statusBadge.classList.remove('offline', 'connecting', 'syncing', 'online');
        this._statusBadge.classList.add(state);

        // Update status text
        this._statusText.textContent = STATUS_TEXT[state] || 'Unknown';

        // Update details panel
        const detailStatus = this._detailsPanel.querySelector('#collab-detail-status');
        if (detailStatus) {
            detailStatus.textContent = STATUS_TEXT[state] || 'Unknown';
        }

        // Show/hide share button based on state
        if (state === CollabState.ONLINE || state === CollabState.SYNCING) {
            this._shareBtn.style.display = '';
        }
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

        // Update status text to show peer count when online
        if (this._currentState === CollabState.ONLINE) {
            if (this._peerCount > 0) {
                this._statusText.textContent = `Online (${this._peerCount})`;
            } else {
                this._statusText.textContent = 'Online';
            }
        }
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
        this._statusBadge.classList.add('show-details');
        this._updatePeerCount();
        this._updatePeersList();
    }

    /**
     * Hide the details panel
     * @private
     */
    _hideDetails() {
        this._showingDetails = false;
        this._statusBadge.classList.remove('show-details');
    }

    /**
     * Handle share button click
     * @private
     */
    _handleShare() {
        if (!this._collabManager || !this._collabManager.roomId) {
            console.warn('Cannot share: not in a room');
            return;
        }

        // Generate share URL
        const url = new URL(window.location.href);
        url.searchParams.set('room', this._collabManager.roomId);
        const shareUrl = url.toString();

        // Copy to clipboard
        navigator.clipboard.writeText(shareUrl).then(() => {
            this._showShareTooltip();
        }).catch(err => {
            console.error('Failed to copy share link:', err);
            // Fallback: show prompt
            prompt('Share this link with collaborators:', shareUrl);
        });
    }

    /**
     * Show the share tooltip briefly
     * @private
     */
    _showShareTooltip() {
        // Position tooltip below share button
        const btnRect = this._shareBtn.getBoundingClientRect();
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
     * Show the share button (when collaboration is enabled)
     */
    showShareButton() {
        this._shareBtn.style.display = '';
    }

    /**
     * Hide the share button
     */
    hideShareButton() {
        this._shareBtn.style.display = 'none';
    }

    /**
     * Get the current room ID (for share link generation)
     * @returns {string|null}
     */
    getRoomId() {
        return this._collabManager ? this._collabManager.roomId : null;
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
