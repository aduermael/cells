// Presence Module
// Manages ephemeral presence data for collaboration (cursors, selections, user info)
// Presence is NOT persisted - it's purely for real-time awareness

/**
 * Adjectives for random name generation
 */
const ADJECTIVES = [
    'Swift', 'Happy', 'Clever', 'Bold', 'Bright',
    'Quick', 'Wise', 'Noble', 'Brave', 'Kind'
];

/**
 * Animals for random name generation
 */
const ANIMALS = [
    'Fox', 'Bear', 'Eagle', 'Wolf', 'Owl',
    'Lion', 'Hawk', 'Deer', 'Tiger', 'Panda'
];

/**
 * Color palette for user cursors - distinct colors with good contrast
 */
export const USER_COLORS = [
    '#FF5733', // Red-Orange
    '#33FF57', // Green
    '#3357FF', // Blue
    '#FF33F5', // Magenta
    '#33FFF5', // Cyan
    '#F5FF33', // Yellow
    '#FF8C33', // Orange
    '#8C33FF', // Purple
    '#33FF8C', // Mint
    '#FF338C'  // Pink
];

/**
 * Presence update interval in milliseconds (5 Hz)
 */
const PRESENCE_UPDATE_INTERVAL = 200;

/**
 * How long to keep broadcasting after user stops moving (ms)
 */
const PRESENCE_LINGER_TIME = 3000;

/**
 * How long to show remote presence after last update (ms)
 */
const PRESENCE_FADE_TIMEOUT = 3000;

/**
 * Generate a random display name (Adjective + Animal)
 * @returns {string}
 */
export function generateRandomName() {
    const adjective = ADJECTIVES[Math.floor(Math.random() * ADJECTIVES.length)];
    const animal = ANIMALS[Math.floor(Math.random() * ANIMALS.length)];
    return `${adjective} ${animal}`;
}

/**
 * Get a color from the palette based on peer ID
 * Uses a simple hash to consistently assign colors
 * @param {string} peerId
 * @returns {string}
 */
export function getColorForPeer(peerId) {
    let hash = 0;
    for (let i = 0; i < peerId.length; i++) {
        hash = ((hash << 5) - hash) + peerId.charCodeAt(i);
        hash = hash & hash; // Convert to 32bit integer
    }
    return USER_COLORS[Math.abs(hash) % USER_COLORS.length];
}

/**
 * Simple event emitter for presence events
 */
class EventEmitter {
    constructor() {
        this._listeners = {};
    }

    on(event, callback) {
        if (!this._listeners[event]) {
            this._listeners[event] = [];
        }
        this._listeners[event].push(callback);
    }

    off(event, callback) {
        if (!this._listeners[event]) return;
        this._listeners[event] = this._listeners[event].filter(cb => cb !== callback);
    }

    emit(event, ...args) {
        if (!this._listeners[event]) return;
        this._listeners[event].forEach(callback => {
            try {
                callback(...args);
            } catch (err) {
                console.error(`Error in presence event listener for ${event}:`, err);
            }
        });
    }
}

/**
 * Represents presence data for a single user
 * @typedef {Object} PresenceData
 * @property {string} peer_id - Unique peer identifier
 * @property {string} name - Display name
 * @property {string} color - Cursor/selection color (hex)
 * @property {string} sheet_id - Current sheet ID
 * @property {Object|null} cursor - Current cursor position {col: ID, row: ID}
 * @property {Object|null} selection - Selected range {start: {col, row}, end: {col, row}}
 * @property {number} timestamp - Last update timestamp (ms)
 */

/**
 * Manages presence for the local user and tracks remote user presence
 */
export class PresenceManager {
    /**
     * @param {Object} options
     * @param {Object} options.webrtcManager - WebRTCManager instance for sending/receiving presence
     */
    constructor(options) {
        if (!options.webrtcManager) {
            throw new Error('PresenceManager requires webrtcManager option');
        }

        this._webrtcManager = options.webrtcManager;
        this._emitter = new EventEmitter();

        // Local user presence state
        this._localPeerId = null;
        this._localName = null;
        this._localColor = null;
        this._localSheetId = null;
        this._localCursor = null;
        this._localSelection = null;

        // Remote user presence (peerId -> PresenceData)
        this._remotePeers = new Map();

        // Broadcasting state
        this._broadcastInterval = null;
        this._lastActivityTime = 0;
        this._isActive = false;

        // Set up message handler for incoming presence
        this._setupMessageHandler();
    }

    /**
     * Initialize the presence manager with local user info
     * @param {string} peerId
     * @param {string} [name] - Display name (generated if not provided)
     */
    initialize(peerId, name = null) {
        this._localPeerId = peerId;
        this._localName = name || this._loadOrGenerateName();
        this._localColor = getColorForPeer(peerId);

        this._emitter.emit('initialized', {
            peerId: this._localPeerId,
            name: this._localName,
            color: this._localColor
        });
    }

    /**
     * Load display name from sessionStorage or generate a new one
     * @returns {string}
     * @private
     */
    _loadOrGenerateName() {
        const storageKey = 'cells.displayName';
        let name = null;

        try {
            name = sessionStorage.getItem(storageKey);
        } catch (e) {
            // sessionStorage not available
        }

        if (!name) {
            name = generateRandomName();
            try {
                sessionStorage.setItem(storageKey, name);
            } catch (e) {
                // sessionStorage not available
            }
        }

        return name;
    }

    /**
     * Get the local user's peer ID
     * @returns {string|null}
     */
    get localPeerId() {
        return this._localPeerId;
    }

    /**
     * Get the local user's display name
     * @returns {string|null}
     */
    get localName() {
        return this._localName;
    }

    /**
     * Set the local user's display name
     * @param {string} name
     */
    setLocalName(name) {
        this._localName = name;
        try {
            sessionStorage.setItem('cells.displayName', name);
        } catch (e) {
            // sessionStorage not available
        }
        this._emitter.emit('localnamechanged', name);
        // Immediately broadcast the name change
        this._broadcastPresence();
    }

    /**
     * Get the local user's color
     * @returns {string|null}
     */
    get localColor() {
        return this._localColor;
    }

    /**
     * Update the current sheet ID
     * @param {string} sheetId
     */
    setCurrentSheet(sheetId) {
        this._localSheetId = sheetId;
        this._markActivity();
    }

    /**
     * Update the cursor position (active cell)
     * @param {string} colId - Column axis ID
     * @param {string} rowId - Row axis ID
     */
    setCursor(colId, rowId) {
        this._localCursor = { col: colId, row: rowId };
        this._markActivity();
    }

    /**
     * Update the selection range
     * @param {Object} start - Start position {col: ID, row: ID}
     * @param {Object} end - End position {col: ID, row: ID}
     */
    setSelection(start, end) {
        this._localSelection = { start, end };
        this._markActivity();
    }

    /**
     * Clear cursor and selection (e.g., when leaving editor mode)
     */
    clearCursorAndSelection() {
        this._localCursor = null;
        this._localSelection = null;
        this._markActivity();
    }

    /**
     * Mark activity to trigger presence broadcasting
     * @private
     */
    _markActivity() {
        this._lastActivityTime = Date.now();

        if (!this._isActive) {
            this._isActive = true;
            this._startBroadcasting();
        }
    }

    /**
     * Start the presence broadcast interval
     * @private
     */
    _startBroadcasting() {
        if (this._broadcastInterval) return;

        this._broadcastInterval = setInterval(() => {
            const now = Date.now();
            const timeSinceActivity = now - this._lastActivityTime;

            if (timeSinceActivity < PRESENCE_LINGER_TIME) {
                // Still active or within linger time, broadcast
                this._broadcastPresence();
            } else {
                // Activity stopped and linger time passed, stop broadcasting
                this._stopBroadcasting();
            }
        }, PRESENCE_UPDATE_INTERVAL);

        // Broadcast immediately
        this._broadcastPresence();
    }

    /**
     * Stop the presence broadcast interval
     * @private
     */
    _stopBroadcasting() {
        if (this._broadcastInterval) {
            clearInterval(this._broadcastInterval);
            this._broadcastInterval = null;
        }
        this._isActive = false;
    }

    /**
     * Broadcast presence to all connected peers
     * @private
     */
    _broadcastPresence() {
        if (!this._localPeerId) return;

        const presence = {
            type: 'presence',
            peer_id: this._localPeerId,
            name: this._localName,
            color: this._localColor,
            sheet_id: this._localSheetId,
            cursor: this._localCursor,
            selection: this._localSelection,
            timestamp: Date.now()
        };

        const message = JSON.stringify(presence);
        this._webrtcManager.broadcastPresence(message);
    }

    /**
     * Set up handler for incoming presence messages
     * @private
     */
    _setupMessageHandler() {
        this._webrtcManager.on('message', (peerId, channelType, data) => {
            if (channelType === 'presence') {
                this._handlePresenceMessage(peerId, data);
            }
        });

        // Clean up presence when peer disconnects
        this._webrtcManager.on('peerremoved', (peerId) => {
            this._removePeer(peerId);
        });

        this._webrtcManager.on('peerdisconnected', (peerId) => {
            this._removePeer(peerId);
        });
    }

    /**
     * Handle incoming presence message from a peer
     * @param {string} peerId
     * @param {string} data - JSON string
     * @private
     */
    _handlePresenceMessage(peerId, data) {
        try {
            const presence = JSON.parse(data);

            if (presence.type !== 'presence') return;
            if (presence.peer_id !== peerId) {
                console.warn('Presence peer_id mismatch:', presence.peer_id, peerId);
                return;
            }

            const existingPresence = this._remotePeers.get(peerId);
            const isNewPeer = !existingPresence;

            // Update or create presence entry
            this._remotePeers.set(peerId, {
                peer_id: presence.peer_id,
                name: presence.name || 'Unknown',
                color: presence.color || getColorForPeer(peerId),
                sheet_id: presence.sheet_id,
                cursor: presence.cursor,
                selection: presence.selection,
                timestamp: presence.timestamp || Date.now()
            });

            // Emit events
            if (isNewPeer) {
                this._emitter.emit('peerarrived', peerId, this._remotePeers.get(peerId));
            }
            this._emitter.emit('presenceupdated', peerId, this._remotePeers.get(peerId));

        } catch (err) {
            console.error('Failed to handle presence message:', err);
        }
    }

    /**
     * Remove a peer from the presence map
     * @param {string} peerId
     * @private
     */
    _removePeer(peerId) {
        if (this._remotePeers.has(peerId)) {
            const presence = this._remotePeers.get(peerId);
            this._remotePeers.delete(peerId);
            this._emitter.emit('peerleft', peerId, presence);
        }
    }

    /**
     * Get all remote peer presence data
     * @returns {Map<string, PresenceData>}
     */
    getRemotePeers() {
        return new Map(this._remotePeers);
    }

    /**
     * Get remote peer presence data for a specific peer
     * @param {string} peerId
     * @returns {PresenceData|undefined}
     */
    getPeerPresence(peerId) {
        return this._remotePeers.get(peerId);
    }

    /**
     * Get all remote peers on a specific sheet
     * @param {string} sheetId
     * @returns {PresenceData[]}
     */
    getPeersOnSheet(sheetId) {
        const peers = [];
        const now = Date.now();

        for (const [peerId, presence] of this._remotePeers) {
            // Only include peers on this sheet and with recent activity
            if (presence.sheet_id === sheetId &&
                (now - presence.timestamp) < PRESENCE_FADE_TIMEOUT) {
                peers.push(presence);
            }
        }

        return peers;
    }

    /**
     * Get the number of remote peers
     * @returns {number}
     */
    getRemotePeerCount() {
        return this._remotePeers.size;
    }

    /**
     * Check if presence for a peer is stale (should fade out)
     * @param {string} peerId
     * @returns {boolean}
     */
    isPresenceStale(peerId) {
        const presence = this._remotePeers.get(peerId);
        if (!presence) return true;

        const age = Date.now() - presence.timestamp;
        return age >= PRESENCE_FADE_TIMEOUT;
    }

    /**
     * Get the age of a peer's presence in milliseconds
     * @param {string} peerId
     * @returns {number}
     */
    getPresenceAge(peerId) {
        const presence = this._remotePeers.get(peerId);
        if (!presence) return Infinity;
        return Date.now() - presence.timestamp;
    }

    /**
     * Calculate fade opacity for a peer's presence (for gradual fade-out)
     * @param {string} peerId
     * @returns {number} Opacity from 0 to 1
     */
    getPresenceOpacity(peerId) {
        const presence = this._remotePeers.get(peerId);
        if (!presence) return 0;

        const age = Date.now() - presence.timestamp;
        if (age < PRESENCE_LINGER_TIME) {
            return 1.0; // Fully visible during active period
        }

        // Fade out during the fade timeout period
        const fadeProgress = (age - PRESENCE_LINGER_TIME) / (PRESENCE_FADE_TIMEOUT - PRESENCE_LINGER_TIME);
        return Math.max(0, 1 - fadeProgress);
    }

    /**
     * Register an event listener
     * Events:
     * - 'initialized' ({peerId, name, color})
     * - 'localnamechanged' (name)
     * - 'peerarrived' (peerId, presence)
     * - 'presenceupdated' (peerId, presence)
     * - 'peerleft' (peerId, presence)
     * @param {string} event
     * @param {Function} callback
     */
    on(event, callback) {
        this._emitter.on(event, callback);
    }

    /**
     * Remove an event listener
     * @param {string} event
     * @param {Function} callback
     */
    off(event, callback) {
        this._emitter.off(event, callback);
    }

    /**
     * Clean up resources
     */
    destroy() {
        this._stopBroadcasting();
        this._remotePeers.clear();
    }
}
